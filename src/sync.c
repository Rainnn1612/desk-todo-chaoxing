/* sync.c - 学习通作业/考试同步
 *
 * 设计：所有网络与 HTML 解析都交给一个 PowerShell 子进程（脚本作为 RCDATA
 * 资源内嵌在 exe 里，首次同步时释放到 %APPDATA%\DeskTodo\cx_sync.ps1）。
 * 主进程只负责：起进程、按秒轮询进度、读回 sync.json 合并进待办。
 *
 * 这样做的好处是主窗口始终单线程、不阻塞、常驻内存一分不涨；代价是同步那
 * 十几秒里会多出一个 powershell 进程，同步结束即退出。
 */
#include "app.h"

#define SYNC_RC_ID      2
#define SYNC_TICK_MS    1000
#define SYNC_TIMEOUT    600          /* 最多等 600 秒 */
#define SYNC_SEEN_MAX   8192         /* ledger 的宽字符容量 */

static void set_msg(App *a, const WCHAR *s)
{
    wcpy(a->sync_msg, s, 96);
    InvalidateRect(a->hwnd, NULL, FALSE);
}

/* 启动阶段的失败：把原因显示出来，并让它过一会儿自己消失。
   原来这几条路径直接 set_msg 后 return，定时器没起来，那行字会永远挂在
   标题栏上（而且被截断），用户既不知道原因也没法让它消失。 */
static void fail_now(App *a, const WCHAR *s)
{
    set_msg(a, s);
    a->sync_msg_left = 60;                  /* 失败信息留久一点 */
    SetTimer(a->hwnd, T_SYNC, SYNC_TICK_MS, NULL);
}

/* 把内嵌脚本释放到数据目录，返回脚本绝对路径 */
static int ensure_script(App *a, WCHAR *out, int n)
{
    HRSRC   rs;
    HGLOBAL hg;
    const BYTE *p;
    DWORD   sz;
    WCHAR   path[MAX_PATH];
    HANDLE  f;
    DWORD   wr = 0;

    data_path(path, MAX_PATH, L"cx_sync.ps1");
    ensure_data_dir();          /* 目录可能还不存在（全新机器），否则下面必然写失败 */
    rs = FindResourceW(a->hi, MAKEINTRESOURCEW(SYNC_RC_ID), RT_RCDATA);
    if (!rs) return 0;
    hg = LoadResource(a->hi, rs);
    if (!hg) return 0;
    p  = (const BYTE *)LockResource(hg);
    sz = SizeofResource(a->hi, rs);
    if (!p || !sz) return 0;

    /* 磁盘脚本与内嵌资源逐字节相同才复用；不同说明程序升级过，必须覆盖。
       原来「文件存在就复用」会让升级后的新脚本永远生效不了 —— 实测升级后
       跑的还是上一版旧脚本（同步结果全是旧学期的作业，还没了退休名单逻辑）。 */
    {
        int flen = 0;
        char *fdata = read_file(path, &flen);
        if (fdata) {
            if ((DWORD)flen == sz && memcmp(fdata, p, (size_t)sz) == 0) {
                free(fdata);
                wcpy(out, path, n);
                return 1;
            }
            free(fdata);
        }
    }

    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    WriteFile(f, p, sz, &wr, NULL);
    CloseHandle(f);
    if (wr != sz) return 0;
    wcpy(out, path, n);
    return 1;
}

static void read_progress(App *a)
{
    WCHAR path[MAX_PATH];
    char *raw;
    int len = 0;
    WCHAR *w;
    data_path(path, MAX_PATH, L"sync.progress");
    raw = read_file(path, &len);
    if (!raw) return;
    w = u82w(raw);
    free(raw);
    if (!w) return;
    wtrim(w);
    if (w[0]) {
        wcpy(a->sync_msg, L"同步 ", 96);
        wcat(a->sync_msg, w, 96);
    }
    free(w);
}

/* ---------------- 已导入账本，防止删掉后被重新加回来 ---------------- */

static WCHAR *seen_load(void)
{
    WCHAR path[MAX_PATH];
    char *raw;
    int len = 0;
    WCHAR *w;
    data_path(path, MAX_PATH, L"sync_seen.txt");
    raw = read_file(path, &len);
    if (!raw) return NULL;
    w = u82w(raw);
    free(raw);
    if (w && wlen(w) >= SYNC_SEEN_MAX) w[SYNC_SEEN_MAX - 1] = 0;
    return w;
}

static int seen_has(const WCHAR *ledger, const WCHAR *key)
{
    const WCHAR *p = ledger;
    int kl = wlen(key);
    if (!ledger || !kl) return 0;
    while (*p) {
        const WCHAR *e = p;
        while (*e && *e != L'\n') e++;
        if ((int)(e - p) == kl) {
            int i, same = 1;
            for (i = 0; i < kl; i++) if (p[i] != key[i]) { same = 0; break; }
            if (same) return 1;
        }
        p = (*e) ? e + 1 : e;
    }
    return 0;
}

static void seen_append(WCHAR *ledger, const WCHAR *key)
{
    int cur = wlen(ledger);
    int kl  = wlen(key);
    if (cur + kl + 2 >= SYNC_SEEN_MAX) return;
    if (cur) ledger[cur++] = L'\n';
    wcpy(ledger + cur, key, SYNC_SEEN_MAX - cur);
}

static void seen_save(const WCHAR *ledger)
{
    WCHAR path[MAX_PATH];
    char *u8;
    data_path(path, MAX_PATH, L"sync_seen.txt");
    u8 = w2u8(ledger ? ledger : L"");
    if (!u8) return;
    write_atomic(path, u8, (int)strlen(u8));
    free(u8);
}

/* ---------------- 合并 sync.json ---------------- */

static void merge_results(App *a)
{
    WCHAR path[MAX_PATH];
    char *raw;
    int len = 0, added = 0, i, n, nretired = 0, nskipc = 0, nseen = 0, ndup = 0;
    JVal *root, *arr;
    WCHAR *ledger;

    data_path(path, MAX_PATH, L"sync.json");
    raw = read_file(path, &len);
    if (!raw) { fail_now(a, L"同步失败：没有拿到结果"); return; }
    root = json_parse(raw);
    free(raw);
    if (!root) { fail_now(a, L"同步失败：结果不是合法 JSON"); return; }

    if (!json_int(root, L"ok", 0)) {
        const WCHAR *e = json_wstr(root, L"error", L"未知错误");
        wcpy(a->sync_msg, L"同步失败：", 96);
        wcat(a->sync_msg, e, 96);
        json_free(root);
        return;
    }

    ledger = seen_load();
    if (!ledger) {
        ledger = (WCHAR *)calloc(SYNC_SEEN_MAX, sizeof(WCHAR));
        if (!ledger) { json_free(root); return; }
    }
    nretired = json_int(root, L"retired", 0);
    nskipc   = json_int(root, L"skipped", 0);

    arr = json_get(root, L"items");
    n = arr ? arr->n : 0;
    for (i = 0; i < n; i++) {
        JVal *it = arr->items[i];
        const WCHAR *kind, *course, *title, *status, *ddl, *sid;
        WCHAR todo_title[MAX_TITLE], note[256], key[MAX_TITLE + 256];
        SYSTEMTIME st;
        int has_ddl;

        if (!it) continue;
        kind   = json_wstr(it, L"kind",   L"");
        course = json_wstr(it, L"course", L"");
        title  = json_wstr(it, L"title",  L"");
        status = json_wstr(it, L"status", L"");
        ddl    = json_wstr(it, L"ddl",    L"");
        sid    = json_wstr(it, L"sid",    L"");
        if (!title[0]) continue;

        wcpy(key, kind, MAX_TITLE + 256);
        wcat(key, L"|", MAX_TITLE + 256);
        wcat(key, course, MAX_TITLE + 256);
        wcat(key, L"|", MAX_TITLE + 256);
        wcat(key, title, MAX_TITLE + 256);
        wcat(key, L"|", MAX_TITLE + 256);
        wcat(key, ddl, MAX_TITLE + 256);
        if (seen_has(ledger, key)) { nseen++; continue; }

        if (wlen(title) >= MAX_TITLE - 4) {
            wcpy(todo_title, title, MAX_TITLE - 3);
            wcat(todo_title, L"…", MAX_TITLE);
        } else {
            wcpy(todo_title, title, MAX_TITLE);
        }
        if (kind[0] == L'e') {              /* exam */
            WCHAR t2[MAX_TITLE];
            wcpy(t2, L"考试：", MAX_TITLE);
            wcat(t2, todo_title, MAX_TITLE);
            wcpy(todo_title, t2, MAX_TITLE);
        }

        note[0] = 0;
        wcat(note, L"学习通", 256);
        if (course[0]) { wcat(note, L" · ", 256); wcat(note, course, 256); }
        if (status[0]) { wcat(note, L" · ", 256); wcat(note, status, 256); }
        (void)sid;

        has_ddl = ddl[0] && parse_date_str(ddl, &st);
        if (store_add_full(a, todo_title, note, has_ddl, &st)) added++;
        else ndup++;
        seen_append(ledger, key);
    }

    seen_save(ledger);
    free(ledger);
    json_free(root);

    list_build_view(a);
    store_save(a);
    widget_fit(a);
    /* 说清「为什么是 0 条」——只说「新增 0 条」时，用户分不清是
       「真没有新作业」还是「同步根本没跑通」，全新机器上尤其误导。 */
    if (added > 0) {
        wcpy(a->sync_msg, L"同步完成 +", 96);
        wcat_int(a->sync_msg, added, 96);
        wcat(a->sync_msg, L" 条", 96);
    } else if (n == 0) {
        wcpy(a->sync_msg, L"同步完成：无新作业", 96);
    } else {
        wcpy(a->sync_msg, L"同步完成：", 96);
        wcat_int(a->sync_msg, nseen + ndup, 96);
        wcat(a->sync_msg, L" 条已存在", 96);
    }
    if (nretired > 0) {
        wcat(a->sync_msg, L"，旧课 ", 96);
        wcat_int(a->sync_msg, nretired, 96);
        wcat(a->sync_msg, L" 门", 96);
    }
    if (nskipc > 0) {
        wcat(a->sync_msg, L"，", 96);
        wcat_int(a->sync_msg, nskipc, 96);
        wcat(a->sync_msg, L" 门失败", 96);
    }
    InvalidateRect(a->hwnd, NULL, FALSE);
}

/* ---------------- 对外接口 ---------------- */

int sync_has_script(void)
{
    return 1;
}

int sync_start(App *a)
{
    WCHAR script[MAX_PATH], sysroot[MAX_PATH], ps[MAX_PATH], cmd[MAX_PATH * 3];
    WCHAR prog[MAX_PATH];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    char *empty;

    if (a->syncing) return 0;
    if (!ensure_script(a, script, MAX_PATH)) {
        fail_now(a, L"同步失败：内嵌脚本无法释放");
        return 0;
    }
    if (!GetEnvironmentVariableW(L"SystemRoot", sysroot, MAX_PATH)) {
        fail_now(a, L"同步失败：找不到 SystemRoot");
        return 0;
    }
    wcpy(ps, sysroot, MAX_PATH);
    wcat(ps, L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe", MAX_PATH);
    if (GetFileAttributesW(ps) == INVALID_FILE_ATTRIBUTES) {
        fail_now(a, L"同步失败：找不到 powershell.exe");
        return 0;
    }

    /* 清掉上一次的产物，免得读到旧结果 */
    data_path(prog, MAX_PATH, L"sync.json");
    DeleteFileW(prog);
    data_path(prog, MAX_PATH, L"sync.progress");
    empty = w2u8(L"准备中");
    if (empty) {
        write_atomic(prog, empty, (int)strlen(empty));
        free(empty);
    }

    wcpy(cmd, L"\"", MAX_PATH * 3);
    wcat(cmd, ps, MAX_PATH * 3);
    wcat(cmd, L"\" -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"", MAX_PATH * 3);
    wcat(cmd, script, MAX_PATH * 3);
    wcat(cmd, L"\"", MAX_PATH * 3);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessW(ps, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        fail_now(a, L"同步失败：无法启动同步进程");
        return 0;
    }
    CloseHandle(pi.hThread);
    a->sync_proc = pi.hProcess;
    a->syncing = 1;
    a->sync_ticks = 0;
    a->sync_t0 = GetTickCount();
    set_msg(a, L"同步 登录中…");
    SetTimer(a->hwnd, T_SYNC, SYNC_TICK_MS, NULL);
    return 1;
}

void sync_tick(App *a)
{
    DWORD code = 0;
    int done = 0;

    if (!a->syncing) {
        /* 同步结束后状态文字再留 20 秒，然后恢复成「待办 x/y」 */
        if (a->sync_msg_left > 0) {
            if (--a->sync_msg_left == 0) {
                a->sync_msg[0] = 0;
                InvalidateRect(a->hwnd, NULL, FALSE);
                KillTimer(a->hwnd, T_SYNC);
            }
        } else {
            KillTimer(a->hwnd, T_SYNC);
        }
        return;
    }
    a->sync_ticks++;
    if (a->sync_proc && GetExitCodeProcess(a->sync_proc, &code) &&
        code != STILL_ACTIVE) {
        done = 1;
    }
    if (!done && a->sync_ticks > SYNC_TIMEOUT) {
        done = 1;
        if (a->sync_proc) TerminateProcess(a->sync_proc, 1);
    }

    if (!done) {
        read_progress(a);
        InvalidateRect(a->hwnd, NULL, FALSE);
        return;
    }

    if (a->sync_proc) { CloseHandle(a->sync_proc); a->sync_proc = NULL; }
    a->syncing = 0;
    merge_results(a);
    a->sync_msg_left = 20;       /* 保留 20 秒后再收起来 */
    InvalidateRect(a->hwnd, NULL, FALSE);
}

void sync_cancel(App *a)
{
    if (a->sync_proc) {
        TerminateProcess(a->sync_proc, 0);
        CloseHandle(a->sync_proc);
        a->sync_proc = NULL;
    }
    a->syncing = 0;
    KillTimer(a->hwnd, T_SYNC);
}
