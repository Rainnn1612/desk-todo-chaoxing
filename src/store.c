/* store.c - 字符串工具、日期解析、数据模型、JSON 落盘、开机自启 */
#include "app.h"

/* ================= 字符串 / 文件工具 ================= */

int wlen(const WCHAR *s) { int n = 0; while (s && s[n]) n++; return n; }

void wcpy(WCHAR *d, const WCHAR *s, int n)
{
    int i = 0;
    if (n <= 0) return;
    if (s) for (; s[i] && i < n - 1; i++) d[i] = s[i];
    d[i] = 0;
}

void wcat(WCHAR *d, const WCHAR *s, int n)
{
    int l = wlen(d), i = 0;
    if (!s) return;
    for (; s[i] && l + i < n - 1; i++) d[l + i] = s[i];
    d[l + i] = 0;
}

void wcat_int(WCHAR *d, int v, int n)
{
    WCHAR tmp[16];
    int i = 0, j, neg = 0;
    unsigned u;
    if (v < 0) { neg = 1; u = (unsigned)(-v); } else u = (unsigned)v;
    do { tmp[i++] = (WCHAR)(L'0' + (u % 10)); u /= 10; } while (u && i < 15);
    if (neg) tmp[i++] = L'-';
    for (j = 0; j < i / 2; j++) { WCHAR t = tmp[j]; tmp[j] = tmp[i - 1 - j]; tmp[i - 1 - j] = t; }
    tmp[i] = 0;
    wcat(d, tmp, n);
}

void wtrim(WCHAR *s)
{
    int n = wlen(s), i = 0, j;
    while (n > 0 && (s[0] == L' ' || s[0] == L'\t')) { for (j = 0; j < n; j++) s[j] = s[j + 1]; n--; }
    while (n > 0 && (s[n - 1] == L' ' || s[n - 1] == L'\t')) s[--n] = 0;
    (void)i;
}

void ensure_dir(const WCHAR *path)
{
    WCHAR tmp[MAX_PATH];
    int i, n;
    wcpy(tmp, path, MAX_PATH);
    n = wlen(tmp);
    for (i = 3; i < n; i++) {
        if (tmp[i] == L'\\' || tmp[i] == L'/') {
            WCHAR c = tmp[i];
            tmp[i] = 0;
            CreateDirectoryW(tmp, NULL);
            tmp[i] = c;
        }
    }
    CreateDirectoryW(tmp, NULL);
}

/* 保证 %APPDATA%\DeskTodo 存在。
   全新电脑上这个目录一开始并不存在，而 login 的 cx.dat、同步器释放的
   cx_sync.ps1、同步结果 sync.json 都直接写在这个目录下——不先建目录，
   第一次「立即同步」会在释放脚本那一步就失败（实测：全新机器上
   标题栏显示「同步失败：内嵌脚本无法释放」，一条待办都进不来）。 */
void ensure_data_dir(void)
{
    WCHAR dir[MAX_PATH];
    dir[0] = 0;
    data_path(dir, MAX_PATH, NULL);
    if (dir[0]) ensure_dir(dir);
}

int write_atomic(const WCHAR *path, const char *data, int len)
{
    WCHAR tmp[MAX_PATH];
    HANDLE h;
    DWORD wr = 0;
    wcpy(tmp, path, MAX_PATH);
    wcat(tmp, L".tmp", MAX_PATH);
    h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (len > 0 && !WriteFile(h, data, (DWORD)len, &wr, NULL)) { CloseHandle(h); DeleteFileW(tmp); return 0; }
    CloseHandle(h);
    if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp); return 0; }
    return 1;
}

char *read_file(const WCHAR *path, int *lenout)
{
    HANDLE h;
    DWORD sz, rd = 0;
    char *buf;
    if (lenout) *lenout = 0;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz > 4 * 1024 * 1024) { CloseHandle(h); return NULL; }
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { CloseHandle(h); return NULL; }
    if (sz && !ReadFile(h, buf, sz, &rd, NULL)) { free(buf); CloseHandle(h); return NULL; }
    CloseHandle(h);
    buf[rd] = 0;
    /* 记事本另存「UTF-8 带 BOM」会在开头塞 EF BB BF，解析器会当成非法字符，
       结果是整个列表变空、一保存就把用户数据冲掉——这里直接跳过。 */
    if (rd >= 3 && (unsigned char)buf[0] == 0xEF
                && (unsigned char)buf[1] == 0xBB
                && (unsigned char)buf[2] == 0xBF) {
        memmove(buf, buf + 3, (size_t)(rd - 3) + 1);
        rd -= 3;
    }
    if (lenout) *lenout = (int)rd;
    return buf;
}

int exe_path(WCHAR *out, int n)
{
    return GetModuleFileNameW(NULL, out, (DWORD)n) > 0;
}

void data_path(WCHAR *out, int n, const WCHAR *file)
{
    WCHAR dir[MAX_PATH];
    out[0] = 0;
    dir[0] = 0;
    if (!GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH) || !dir[0]) {
        /* 兜底：C:\Users\<user>\AppData\Roaming */
        WCHAR prof[MAX_PATH];
        DWORD cl = MAX_PATH;
        if (!GetUserNameW(prof, &cl)) return;
        if (!GetEnvironmentVariableW(L"USERPROFILE", dir, MAX_PATH) || !dir[0]) return;
        wcat(dir, L"\\AppData\\Roaming", MAX_PATH);
    }
    wcpy(out, dir, n);
    wcat(out, L"\\", n);
    wcat(out, DATA_DIR, n);
    if (file) { wcat(out, L"\\", n); wcat(out, file, n); }
}

/* ================= 开机自启 ================= */

int autostart_get(void)
{
    HKEY k;
    WCHAR buf[MAX_PATH * 2];
    DWORD cb = sizeof(buf), type = 0;
    int ok = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
    if (RegQueryValueExW(k, RUN_NAME, NULL, &type, (LPBYTE)buf, &cb) == ERROR_SUCCESS && type == REG_SZ) ok = 1;
    RegCloseKey(k);
    return ok;
}

void autostart_set(int on)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    if (on) {
        WCHAR exe[MAX_PATH], val[MAX_PATH + 8];
        if (exe_path(exe, MAX_PATH)) {
            wcpy(val, L"\"", MAX_PATH + 8);
            wcat(val, exe, MAX_PATH + 8);
            wcat(val, L"\" --startup", MAX_PATH + 8);
            RegSetValueExW(k, RUN_NAME, 0, REG_SZ, (const BYTE *)val,
                           (DWORD)((wlen(val) + 1) * sizeof(WCHAR)));
        }
    } else {
        RegDeleteValueW(k, RUN_NAME);
    }
    RegCloseKey(k);
}

/* ================= 日期 ================= */

int sys_cmp_time(const SYSTEMTIME *a, const SYSTEMTIME *b)
{
    if (a->wYear != b->wYear) return a->wYear < b->wYear ? -1 : 1;
    if (a->wMonth != b->wMonth) return a->wMonth < b->wMonth ? -1 : 1;
    if (a->wDay != b->wDay) return a->wDay < b->wDay ? -1 : 1;
    if (a->wHour != b->wHour) return a->wHour < b->wHour ? -1 : 1;
    if (a->wMinute != b->wMinute) return a->wMinute < b->wMinute ? -1 : 1;
    return 0;
}

static void add_days(SYSTEMTIME *s, int d)
{
    FILETIME ft;
    ULARGE_INTEGER u;
    if (!SystemTimeToFileTime(s, &ft)) {
        SYSTEMTIME now; GetLocalTime(&now);
        now.wHour = now.wMinute = now.wSecond = now.wMilliseconds = 0;
        *s = now;
        SystemTimeToFileTime(s, &ft);
    }
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    u.QuadPart += (ULONGLONG)(LONGLONG)d * 864000000000ULL;
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    FileTimeToSystemTime(&ft, s);
}

static int digits(const WCHAR **p)
{
    int v = 0, n = 0;
    while (**p >= L'0' && **p <= L'9') {
        v = v * 10 + (**p - L'0');
        (*p)++;
        if (++n > 4) return -1;
    }
    return n ? v : -1;
}

static int time_only(SYSTEMTIME *o)
{
    SYSTEMTIME now;
    GetLocalTime(&now);
    if (o->wYear == 0) *o = now;
    o->wHour = 0; o->wMinute = 0; o->wSecond = 0; o->wMilliseconds = 0;
    return 1;
}

/* 构造 y-m-d 零点，日先归 1 再整体偏移，自动处理大小月/跨年 */
static void mk_date(SYSTEMTIME *o, int y, int m, int d)
{
    SYSTEMTIME s;
    GetLocalTime(&s);
    while (m > 12) { m -= 12; y++; }
    while (m < 1)  { m += 12; y--; }
    s.wYear = (WORD)y;
    s.wMonth = (WORD)m;
    s.wDay = 1;
    s.wHour = 0; s.wMinute = 0; s.wSecond = 0; s.wMilliseconds = 0;
    add_days(&s, d - 1);
    *o = s;
}

/* a 是否早于今天(零点) */
static int date_past(const SYSTEMTIME *a)
{
    SYSTEMTIME t;
    FILETIME f1, f2;
    GetLocalTime(&t);
    t.wHour = 0; t.wMinute = 0; t.wSecond = 0; t.wMilliseconds = 0;
    if (!SystemTimeToFileTime(a, &f1) || !SystemTimeToFileTime(&t, &f2)) return 0;
    return CompareFileTime(&f1, &f2) < 0;
}

static int parse_time_tok(const WCHAR *t, int *hh, int *mm)
{
    const WCHAR *p = t;
    int a, b;
    a = digits(&p);
    if (a < 0 || *p != L':') return 0;
    p++;
    b = digits(&p);
    if (b < 0 || *p) return 0;
    if (a > 23 || b > 59) return 0;
    *hh = a; *mm = b;
    return 1;
}

static int parse_date_tok(const WCHAR *t, SYSTEMTIME *o)
{
    SYSTEMTIME now;
    const WCHAR *p = t, *q;
    int a, b, c;
    GetLocalTime(&now);

    if (!wcscmp(t, L"今天")) { *o = now; return time_only(o); }
    if (!wcscmp(t, L"明天") || !wcscmp(t, L"明日")) { *o = now; add_days(o, 1); return time_only(o); }
    if (!wcscmp(t, L"后天")) { *o = now; add_days(o, 2); return time_only(o); }
    if (!wcscmp(t, L"大后天")) { *o = now; add_days(o, 3); return time_only(o); }

    /* N月N日 / N月N号（可带 今年/明年/后年 前缀） */
    {
        const WCHAR *r = t;
        int yadd = 0, fixed = 0;
        if (!wcsncmp(r, L"明年", 2)) { yadd = 1; r += 2; fixed = 1; }
        else if (!wcsncmp(r, L"今年", 2)) { yadd = 0; r += 2; fixed = 1; }
        else if (!wcsncmp(r, L"后年", 2)) { yadd = 2; r += 2; fixed = 1; }
        {
            const WCHAR *pp = r;
            int m = digits(&pp), dd;
            if (m >= 1 && m <= 12 && *pp == L'月') {
                pp++;
                dd = digits(&pp);
                if (dd >= 1 && dd <= 31 && (*pp == L'日' || *pp == L'号') && pp[1] == 0) {
                    SYSTEMTIME cand;
                    mk_date(&cand, now.wYear + yadd, m, dd);
                    if (cand.wMonth != (WORD)m || cand.wDay != (WORD)dd) return 0;
                    if (!fixed && date_past(&cand)) mk_date(&cand, now.wYear + yadd + 1, m, dd);
                    *o = cand;
                    return 1;
                }
            }
        }
    }

    q = t;
    {
        int next = 0, wd = -1;
        if (*q == L'下') { next = 1; q++; }
        if (*q == L'周') q++;
        else if (q[0] == L'星' && q[1] == L'期') q += 2;
        else q = NULL;
        if (q && *q) {
            switch (*q) {
            case L'一': wd = 1; break; case L'二': wd = 2; break;
            case L'三': wd = 3; break; case L'四': wd = 4; break;
            case L'五': wd = 5; break; case L'六': wd = 6; break;
            case L'日': case L'天': wd = 0; break;
            default: wd = -1; break;
            }
            if (wd >= 0 && q[1] == 0) {
                int cur = now.wDayOfWeek;
                int delta = (wd - cur + 7) % 7;
                if (delta == 0) delta = 7;
                if (next) delta += 7;
                *o = now;
                add_days(o, delta);
                return time_only(o);
            }
        }
    }

    p = t;
    a = digits(&p);
    if (a < 0) return 0;

    if (*p == L'-' || *p == L'/' || *p == L'.') {
        WCHAR sep = *p;
        p++;
        b = digits(&p);
        if (b < 0) return 0;
        if (*p == sep) {
            p++;
            c = digits(&p);
            if (c < 0 || *p) return 0;
            if (a < 1900 || a > 2999 || b < 1 || b > 12 || c < 1 || c > 31) return 0;
            mk_date(o, a, b, c);
            if (o->wYear != (WORD)a || o->wMonth != (WORD)b || o->wDay != (WORD)c) return 0;
            return 1;
        }
        if (*p == 0) {
            SYSTEMTIME cand;
            if (a < 1 || a > 12 || b < 1 || b > 31) return 0;
            mk_date(&cand, now.wYear, a, b);
            if (cand.wMonth != (WORD)a || cand.wDay != (WORD)b) return 0;
            if (date_past(&cand)) {
                mk_date(&cand, now.wYear + 1, a, b);
                if (cand.wMonth != (WORD)a || cand.wDay != (WORD)b) return 0;
            }
            *o = cand;
            return 1;
        }
        return 0;
    }

    if ((*p == L'日' || *p == L'号') && p[1] == 0) {
        SYSTEMTIME cand;
        if (a < 1 || a > 31) return 0;
        mk_date(&cand, now.wYear, now.wMonth, a);
        if (date_past(&cand)) mk_date(&cand, now.wYear, now.wMonth + 1, a);
        *o = cand;
        return 1;
    }
    return 0;
}

void parse_todo_text(const WCHAR *in, WCHAR *title, int *has_ddl, SYSTEMTIME *ddl)
{
    WCHAR toks[MAXTOK][64];
    int nt = 0, start = 0, end, i, k = 0, guard;
    const WCHAR *p = in;
    int hasd = 0, hast = 0, th = -1, tm = -1;
    SYSTEMTIME dt;

    *has_ddl = 0;
    title[0] = 0;

    while (*p && nt < MAXTOK) {
        int i2 = 0;
        while (*p == L' ' || *p == L'\t' || *p == 0x3000) p++;
        if (!*p) break;
        while (*p && *p != L' ' && *p != L'\t' && *p != 0x3000 && i2 < 63) toks[nt][i2++] = *p++;
        toks[nt][i2] = 0;
        nt++;
    }
    end = nt;
    if (end == 0) return;

    for (guard = 0; guard < 4 && end > start; guard++) {
        const WCHAR *t = toks[end - 1];
        if (!hasd && parse_date_tok(t, &dt)) { hasd = 1; end--; continue; }
        if (!hast && parse_time_tok(t, &th, &tm)) { hast = 1; end--; continue; }
        break;
    }

    /* 日期/时间写在开头的情况：「明天 交报告」「9:30 开会」 */
    for (guard = 0; guard < 2 && end > start; guard++) {
        const WCHAR *t = toks[start];
        if (!hasd && parse_date_tok(t, &dt)) { hasd = 1; start++; continue; }
        if (!hast && parse_time_tok(t, &th, &tm)) { hast = 1; start++; continue; }
        break;
    }

    if (hasd || hast) {
        SYSTEMTIME s;
        if (hasd) s = dt;
        else { GetLocalTime(&s); s.wHour = 0; s.wMinute = 0; }
        s.wHour = (WORD)(hast ? th : 0);
        s.wMinute = (WORD)(hast ? tm : 0);
        s.wSecond = 0; s.wMilliseconds = 0;
        *ddl = s;
        *has_ddl = 1;
    }

    for (i = start; i < end; i++) {
        const WCHAR *q;
        if (i > start && k < MAX_TITLE - 1) title[k++] = L' ';
        for (q = toks[i]; *q && k < MAX_TITLE - 1; q++) title[k++] = *q;
    }
    title[k] = 0;
    wtrim(title);
}

int ddl_days(const Todo *t)
{
    SYSTEMTIME a, b;
    FILETIME f1, f2;
    ULARGE_INTEGER u1, u2;
    LONGLONG d;
    if (!t->has_ddl) return 0x7fffffff;
    GetLocalTime(&b);
    a = t->ddl;
    a.wHour = 0; a.wMinute = 0; a.wSecond = 0; a.wMilliseconds = 0;
    b.wHour = 0; b.wMinute = 0; b.wSecond = 0; b.wMilliseconds = 0;
    if (!SystemTimeToFileTime(&a, &f1) || !SystemTimeToFileTime(&b, &f2)) return 0;
    u1.LowPart = f1.dwLowDateTime; u1.HighPart = f1.dwHighDateTime;
    u2.LowPart = f2.dwLowDateTime; u2.HighPart = f2.dwHighDateTime;
    d = ((LONGLONG)u1.QuadPart - (LONGLONG)u2.QuadPart) / 864000000000LL;
    if (d > 100000) d = 100000;
    if (d < -100000) d = -100000;
    return (int)d;
}

int ddl_level(const Todo *t)
{
    int d;
    if (!t->has_ddl) return 0;
    d = ddl_days(t);
    if (d < 0) return 4;      /* 逾期 */
    if (d == 0) return 3;     /* 今天 */
    if (d <= 3) return 2;     /* 3 天内 */
    return 1;
}

void ddl_text(const Todo *t, WCHAR *buf, int n)
{
    int d;
    buf[0] = 0;
    if (!t->has_ddl) return;
    d = ddl_days(t);
    if (d < 0) { wcpy(buf, L"逾期", n); wcat_int(buf, -d, n); wcat(buf, L"天", n); return; }
    if (d == 0) { wcpy(buf, L"今天", n); return; }
    if (d == 1) { wcpy(buf, L"明天", n); return; }
    wcpy(buf, L"剩", n);
    wcat_int(buf, d, n);
    wcat(buf, L"天", n);
}

/* ================= 数据模型 ================= */

void store_init(App *a)
{
    a->todos.v = NULL; a->todos.n = 0; a->todos.cap = 0;
    a->todos.view = NULL; a->todos.vcap = 0;
    a->cfg.x = -1; a->cfg.y = -1; a->cfg.width = 300;
    a->cfg.topmost = 1; a->cfg.autostart = 0; a->cfg.theme = 0;
    a->cfg.manual_order = 0; a->cfg.edge = 0; a->cfg.peek = 0;
    a->cfg.font = 100;
    theme_defaults(a);
    a->cfg.glass = 0;
    a->editing = -1; a->edit_new = 0; a->editlen = 0; a->caret = 0;
    a->edit[0] = 0;
}

void store_free(App *a)
{
    int i;
    for (i = 0; i < a->todos.n; i++) {
        free(a->todos.v[i].note); a->todos.v[i].note = NULL;
        free(a->todos.v[i].img);  a->todos.v[i].img = NULL;
    }
    free(a->todos.v); a->todos.v = NULL;
    free(a->todos.view); a->todos.view = NULL;
    a->todos.n = a->todos.cap = a->todos.vcap = 0;
}

static int list_less(App *a, int i, int j)
{
    Todo *x = &a->todos.v[i], *y = &a->todos.v[j];
    if (x->done != y->done) return x->done < y->done;
    if (x->has_ddl != y->has_ddl) return x->has_ddl > y->has_ddl;
    if (x->has_ddl && y->has_ddl) {
        int c = sys_cmp_time(&x->ddl, &y->ddl);
        if (c) return c < 0;
    }
    return i < j;
}

void list_build_view(App *a)
{
    int i, j, n = a->todos.n;
    if (a->todos.vcap < n) {
        a->todos.view = (int *)realloc(a->todos.view, (size_t)(n + 8) * sizeof(int));
        a->todos.vcap = n + 8;
    }
    for (i = 0; i < n; i++) a->todos.view[i] = i;
    if (a->todos.view && !a->cfg.manual_order) {
        for (i = 1; i < n; i++) {          /* 插入排序，n<=512 */
            int key = a->todos.view[i];
            for (j = i - 1; j >= 0 && list_less(a, key, a->todos.view[j]); j--)
                a->todos.view[j + 1] = a->todos.view[j];
            a->todos.view[j + 1] = key;
        }
    }
}

void list_materialize_view(App *a)
{
    Todo *nv;
    int i, n = a->todos.n;
    if (n <= 0 || !a->todos.view) return;
    nv = (Todo *)malloc((size_t)n * sizeof(Todo));
    if (!nv) return;
    for (i = 0; i < n; i++) nv[i] = a->todos.v[a->todos.view[i]];
    memcpy(a->todos.v, nv, (size_t)n * sizeof(Todo));
    free(nv);
}

void list_reorder(App *a, int from_view, int to_view)
{
    int *old;
    int i, n = a->todos.n;
    if (from_view < 0 || from_view >= n || to_view < 0 || to_view > n) return;
    if (from_view == to_view || from_view == to_view - 1) return;
    old = (int *)malloc((size_t)n * sizeof(int));
    if (!old) return;
    memcpy(old, a->todos.view, (size_t)n * sizeof(int));
    {
        int w = 0;
        for (i = 0; i < n; i++) {
            if (i == from_view) continue;
            if (w == to_view) a->todos.view[w++] = old[from_view];
            a->todos.view[w++] = old[i];
        }
        if (w < n) a->todos.view[w++] = old[from_view];
    }
    free(old);
    list_materialize_view(a);
    a->cfg.manual_order = 1;
    list_build_view(a);          /* 数组已按新顺序固化，view 必须重置为恒等映射 */
}

static Todo *list_push(App *a)
{
    if (a->todos.n >= MAX_TITLE * 4) return NULL;
    if (a->todos.n >= a->todos.cap) {
        int nc = a->todos.cap ? a->todos.cap * 2 : 16;
        Todo *nv = (Todo *)realloc(a->todos.v, (size_t)nc * sizeof(Todo));
        if (!nv) return NULL;
        a->todos.v = nv;
        a->todos.cap = nc;
    }
    memset(&a->todos.v[a->todos.n], 0, sizeof(Todo));
    return &a->todos.v[a->todos.n++];
}

void store_add(App *a, const WCHAR *text)
{
    WCHAR title[MAX_TITLE];
    int has_ddl;
    SYSTEMTIME ddl;
    Todo *t;
    parse_todo_text(text, title, &has_ddl, &ddl);
    if (!title[0]) return;
    t = list_push(a);
    if (!t) return;
    wcpy(t->title, title, MAX_TITLE);
    t->has_ddl = has_ddl;
    t->ddl = ddl;
    t->done = 0;
    list_build_view(a);
    store_save(a);
}

/* 给同步用：标题和 DDL 都是现成的，另可带一段说明（课程名/状态）。
   成功返回 1；已存在同名（不区分大小写）或失败返回 0。 */
int store_add_full(App *a, const WCHAR *title, const WCHAR *note,
                   int has_ddl, const SYSTEMTIME *ddl)
{
    Todo *t;
    int i;
    if (!title || !title[0]) return 0;
    for (i = 0; i < a->todos.n; i++)
        if (_wcsicmp(a->todos.v[i].title, title) == 0) return 0;
    t = list_push(a);
    if (!t) return 0;
    wcpy(t->title, title, MAX_TITLE);
    t->has_ddl = has_ddl ? 1 : 0;
    if (has_ddl && ddl) t->ddl = *ddl;
    t->done = 0;
    if (note && note[0]) t->note = wdup(note);
    return 1;
}

void store_update(App *a, int ti, const WCHAR *text)
{
    WCHAR title[MAX_TITLE];
    int has_ddl;
    SYSTEMTIME ddl;
    if (ti < 0 || ti >= a->todos.n) return;
    parse_todo_text(text, title, &has_ddl, &ddl);
    if (!title[0]) { store_remove(a, ti); return; }
    wcpy(a->todos.v[ti].title, title, MAX_TITLE);
    a->todos.v[ti].has_ddl = has_ddl;
    a->todos.v[ti].ddl = ddl;
    list_build_view(a);
    store_save(a);
}

void store_remove(App *a, int ti)
{
    int i;
    if (ti < 0 || ti >= a->todos.n) return;
    free(a->todos.v[ti].note); a->todos.v[ti].note = NULL;
    free(a->todos.v[ti].img);  a->todos.v[ti].img = NULL;
    for (i = ti; i < a->todos.n - 1; i++) a->todos.v[i] = a->todos.v[i + 1];
    a->todos.n--;
    memset(&a->todos.v[a->todos.n], 0, sizeof(Todo));
    list_build_view(a);
    store_save(a);
    img_gc(a);
}

/* ================= 说明文字 / 图片 ================= */

WCHAR *wdup(const WCHAR *s)
{
    int n;
    WCHAR *p;
    if (!s) return NULL;
    n = wlen(s);
    p = (WCHAR *)malloc((size_t)(n + 1) * sizeof(WCHAR));
    if (!p) return NULL;
    memcpy(p, s, (size_t)(n + 1) * sizeof(WCHAR));
    return p;
}

void img_path_of(const WCHAR *name, WCHAR *out, int n)
{
    int i, j;
    data_path(out, n, NULL);
    wcat(out, L"\\img\\", n);
    j = wlen(out);
    for (i = 0; name && name[i] && j < n - 1; i++) out[j++] = name[i];
    out[j] = 0;
}

/* 把选中的图片复制进数据目录，返回新文件名（img1.png 这种）。
   只存文件名不存原始路径，原图被移动/删除也不影响。 */
int img_import(const WCHAR *src, WCHAR *out, int n)
{
    WCHAR dir[MAX_PATH], dst[MAX_PATH], ext[8];
    const WCHAR *dot = NULL;
    int i, k = 0;

    if (!src || !src[0]) return 0;
    for (i = 0; src[i]; i++) if (src[i] == L'.') dot = src + i;
    ext[0] = 0;
    if (dot) {
        for (k = 0; k < 5 && dot[k + 1]; k++) {
            WCHAR c = dot[k + 1];
            if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                  (c >= L'0' && c <= L'9'))) break;
            ext[k] = (c >= L'A' && c <= L'Z') ? (WCHAR)(c - L'A' + L'a') : c;
        }
    }
    ext[k] = 0;

    data_path(dir, MAX_PATH, NULL);
    wcpy(dst, dir, MAX_PATH);
    wcat(dst, L"\\img", MAX_PATH);
    ensure_dir(dst);

    for (i = 1; i < 100000; i++) {
        wcpy(dst, dir, MAX_PATH);
        wcat(dst, L"\\img\\img", MAX_PATH);
        wcat_int(dst, i, MAX_PATH);
        if (ext[0]) { wcat(dst, L".", MAX_PATH); wcat(dst, ext, MAX_PATH); }
        if (GetFileAttributesW(dst) != INVALID_FILE_ATTRIBUTES) continue;
        if (!CopyFileW(src, dst, TRUE)) return 0;
        if (out && n > 0) {
            wcpy(out, L"img", n);
            wcat_int(out, i, n);
            if (ext[0]) { wcat(out, L".", n); wcat(out, ext, n); }
        }
        return 1;
    }
    return 0;
}

/* 删掉 img 目录里已经没有任何待办引用到的图片（换图、删待办后留下的垃圾） */
void img_gc(App *a)
{
    WCHAR dir[MAX_PATH], pat[MAX_PATH], full[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int i;

    data_path(dir, MAX_PATH, NULL);
    wcpy(pat, dir, MAX_PATH);
    wcat(pat, L"\\img\\*", MAX_PATH);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        int used = 0;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        for (i = 0; i < a->todos.n; i++) {
            const WCHAR *im = a->todos.v[i].img;
            if (im && im[0] && !lstrcmpiW(im, fd.cFileName)) { used = 1; break; }
        }
        if (!used) {
            wcpy(full, dir, MAX_PATH);
            wcat(full, L"\\img\\", MAX_PATH);
            wcat(full, fd.cFileName, MAX_PATH);
            DeleteFileW(full);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* 说明文字压成一行，给列表里当标题后缀用 */
void note_head(const WCHAR *note, WCHAR *buf, int n)
{
    int i = 0, j = 0, sp = 0;
    if (n <= 0) return;
    buf[0] = 0;
    if (!note) return;
    while (note[i] == L' ' || note[i] == L'\r' || note[i] == L'\n' || note[i] == L'\t') i++;
    for (; note[i] && j < n - 1; i++) {
        WCHAR c = note[i];
        if (c == L'\r' || c == L'\n' || c == L'\t' || c == L' ') { sp = 1; continue; }
        if (sp && j > 0) buf[j++] = L' ';
        sp = 0;
        buf[j++] = c;
    }
    buf[j] = 0;
    while (j > 0 && buf[j - 1] == L' ') buf[--j] = 0;
}

int store_set_detail(App *a, int ti, const WCHAR *note, const WCHAR *img)
{
    Todo *t;
    WCHAR *nn = NULL, *ni = NULL;
    if (ti < 0 || ti >= a->todos.n) return 0;
    if (note && note[0]) { nn = wdup(note); if (!nn) return 0; }
    if (img && img[0]) { ni = wdup(img); if (!ni) { free(nn); return 0; } }
    t = &a->todos.v[ti];
    free(t->note); t->note = nn;
    free(t->img);  t->img = ni;
    store_save(a);
    img_gc(a);
    return 1;
}

/* ================= 落盘 ================= */

typedef struct { char *p; int len, cap; } SB;

static void sb_need(SB *b, int extra)
{
    if (b->len + extra + 1 > b->cap) {
        while (b->len + extra + 1 > b->cap) b->cap = b->cap ? b->cap * 2 : 1024;
        b->p = (char *)realloc(b->p, (size_t)b->cap);
    }
}

static void sb_puts(SB *b, const char *s) { int n = (int)strlen(s); sb_need(b, n); memcpy(b->p + b->len, s, (size_t)n); b->len += n; b->p[b->len] = 0; }
static void sb_putc(SB *b, char c) { sb_need(b, 1); b->p[b->len++] = c; b->p[b->len] = 0; }

static void sb_putint(SB *b, int v)
{
    char tmp[16];
    int i = 0, j, neg = 0;
    unsigned u;
    if (v < 0) { neg = 1; u = (unsigned)(-v); } else u = (unsigned)v;
    do { tmp[i++] = (char)('0' + (u % 10)); u /= 10; } while (u && i < 15);
    if (neg) tmp[i++] = '-';
    sb_need(b, i + 1);
    for (j = 0; j < i; j++) b->p[b->len + j] = tmp[i - 1 - j];
    b->len += i;
    b->p[b->len] = 0;
}

static void sb_putw(SB *b, const WCHAR *w)
{
    sb_putc(b, '"');
    while (*w) {
        unsigned cp = (unsigned)*w++;
        if (cp == L'"' || cp == L'\\') { sb_putc(b, '\\'); sb_putc(b, (char)cp); }
        else if (cp == L'\n') sb_puts(b, "\\n");
        else if (cp == L'\r') sb_puts(b, "\\r");
        else if (cp == L'\t') sb_puts(b, "\\t");
        else if (cp < 0x20) {
            const char *hx = "0123456789abcdef";
            sb_puts(b, "\\u00");
            sb_putc(b, hx[(cp >> 4) & 0xF]);
            sb_putc(b, hx[cp & 0xF]);
        } else if (cp >= 0xD800 && cp <= 0xDBFF && *w >= 0xDC00 && *w <= 0xDFFF) {
            unsigned lo = (unsigned)*w++;
            unsigned full = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            char u8[4];
            int k, n;
            if (full < 0x80) n = 1;
            else if (full < 0x800) n = 2;
            else if (full < 0x10000) n = 3;
            else n = 4;
            if (n == 1) { u8[0] = (char)full; }
            else if (n == 2) { u8[0] = (char)(0xC0 | (full >> 6)); u8[1] = (char)(0x80 | (full & 0x3F)); }
            else if (n == 3) { u8[0] = (char)(0xE0 | (full >> 12)); u8[1] = (char)(0x80 | ((full >> 6) & 0x3F)); u8[2] = (char)(0x80 | (full & 0x3F)); }
            else { u8[0] = (char)(0xF0 | (full >> 18)); u8[1] = (char)(0x80 | ((full >> 12) & 0x3F)); u8[2] = (char)(0x80 | ((full >> 6) & 0x3F)); u8[3] = (char)(0x80 | (full & 0x3F)); }
            for (k = 0; k < n; k++) sb_putc(b, u8[k]);
        } else if (cp < 0x80) {
            sb_putc(b, (char)cp);
        } else {
            char u8[3];
            if (cp < 0x800) { u8[0] = (char)(0xC0 | (cp >> 6)); u8[1] = (char)(0x80 | (cp & 0x3F)); sb_putc(b, u8[0]); sb_putc(b, u8[1]); }
            else { u8[0] = (char)(0xE0 | (cp >> 12)); u8[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); u8[2] = (char)(0x80 | (cp & 0x3F)); sb_putc(b, u8[0]); sb_putc(b, u8[1]); sb_putc(b, u8[2]); }
        }
    }
    sb_putc(b, '"');
}

/* 把 DDL 还原成可再解析的文本 "YYYY-MM-DD HH:MM"（行内编辑时拼到标题后面，
 * 这样编辑标题不会丢失 DDL，删掉这段文本就等于删除 DDL）。 */
void ddl_to_text(const SYSTEMTIME *s, WCHAR *buf, int n)
{
    WCHAR tmp[32];
    int vals[5], wid[5], sep[5], i = 0, k;
    vals[0] = s->wYear;  wid[0] = 4; sep[0] = L'-';
    vals[1] = s->wMonth; wid[1] = 2; sep[1] = L'-';
    vals[2] = s->wDay;   wid[2] = 2; sep[2] = L' ';
    vals[3] = s->wHour;  wid[3] = 2; sep[3] = L':';
    vals[4] = s->wMinute; wid[4] = 2; sep[4] = 0;
    for (k = 0; k < 5; k++) {
        int v = vals[k], w = wid[k], j;
        WCHAR d[8];
        for (j = w - 1; j >= 0; j--) { d[j] = (WCHAR)(L'0' + (v % 10)); v /= 10; }
        for (j = 0; j < w && i < 30; j++) tmp[i++] = d[j];
        if (sep[k] && i < 30) tmp[i++] = (WCHAR)sep[k];
    }
    tmp[i] = 0;
    wcpy(buf, tmp, n);
}

static void sb_putdate(SB *b, const SYSTEMTIME *s)
{
    sb_putc(b, '"');
    sb_putint(b, s->wYear); sb_putc(b, '-');
    if (s->wMonth < 10) { sb_putc(b, '0'); }
    sb_putint(b, s->wMonth); sb_putc(b, '-');
    if (s->wDay < 10) { sb_putc(b, '0'); }
    sb_putint(b, s->wDay); sb_putc(b, 'T');
    if (s->wHour < 10) { sb_putc(b, '0'); }
    sb_putint(b, s->wHour); sb_putc(b, ':');
    if (s->wMinute < 10) { sb_putc(b, '0'); }
    sb_putint(b, s->wMinute);
    sb_putc(b, '"');
}

int parse_date_str(const WCHAR *s, SYSTEMTIME *o)
{
    const WCHAR *p = s;
    int y, mo, d, h = 0, mi = 0;
    y = digits(&p); if (y < 0 || *p != L'-') return 0; p++;
    mo = digits(&p); if (mo < 0 || *p != L'-') return 0; p++;
    d = digits(&p); if (d < 0) return 0;
    if (*p == L'T' || *p == L' ') {
        p++;
        h = digits(&p);
        if (h < 0 || *p != L':') return 0;
        p++;
        mi = digits(&p);
        if (mi < 0) return 0;
    }
    o->wYear = (WORD)y; o->wMonth = (WORD)mo; o->wDay = (WORD)d;
    o->wHour = (WORD)h; o->wMinute = (WORD)mi;
    o->wSecond = 0; o->wMilliseconds = 0; o->wDayOfWeek = 0;
    return 1;
}

int store_save(App *a)
{
    SB b;
    WCHAR path[MAX_PATH], dir[MAX_PATH];
    int i, ok;
    b.p = NULL; b.len = 0; b.cap = 0;
    sb_puts(&b, "{\n  \"version\": 1,\n  \"todos\": [");
    for (i = 0; i < a->todos.n; i++) {
        Todo *t = &a->todos.v[i];
        sb_puts(&b, i ? ",\n    {" : "\n    {");
        sb_puts(&b, "\"t\": "); sb_putw(&b, t->title);
        if (t->has_ddl) { sb_puts(&b, ", \"ddl\": "); sb_putdate(&b, &t->ddl); }
        if (t->done) sb_puts(&b, ", \"done\": 1");
        if (t->note && t->note[0]) { sb_puts(&b, ", \"note\": "); sb_putw(&b, t->note); }
        if (t->img && t->img[0]) { sb_puts(&b, ", \"img\": "); sb_putw(&b, t->img); }
        sb_putc(&b, '}');
    }
    sb_puts(&b, a->todos.n ? "\n  ]\n}\n" : "]\n}\n");

    data_path(path, MAX_PATH, L"todos.json");
    data_path(dir, MAX_PATH, NULL);
    ensure_dir(dir);
    ok = write_atomic(path, b.p ? b.p : "{}", b.len);
    free(b.p);
    return ok;
}

int cfg_save(App *a)
{
    SB b;
    WCHAR path[MAX_PATH], dir[MAX_PATH];
    int ok;
    b.p = NULL; b.len = 0; b.cap = 0;
    sb_puts(&b, "{\n  \"x\": "); sb_putint(&b, a->cfg.x);
    sb_puts(&b, ",\n  \"y\": "); sb_putint(&b, a->cfg.y);
    sb_puts(&b, ",\n  \"width\": "); sb_putint(&b, a->cfg.width);
    sb_puts(&b, ",\n  \"topmost\": "); sb_putint(&b, a->cfg.topmost);
    sb_puts(&b, ",\n  \"autostart\": "); sb_putint(&b, a->cfg.autostart);
    sb_puts(&b, ",\n  \"theme\": "); sb_putint(&b, a->cfg.theme);
    sb_puts(&b, ",\n  \"manual_order\": "); sb_putint(&b, a->cfg.manual_order);
    sb_puts(&b, ",\n  \"edge\": "); sb_putint(&b, a->cfg.edge);
    sb_puts(&b, ",\n  \"peek\": "); sb_putint(&b, a->cfg.peek);
    sb_puts(&b, ",\n  \"font\": "); sb_putint(&b, a->cfg.font);
    sb_puts(&b, ",\n  \"cl_page\": "); sb_putint(&b, a->cfg.cl_page);
    sb_puts(&b, ",\n  \"cl_bar\": "); sb_putint(&b, a->cfg.cl_bar);
    sb_puts(&b, ",\n  \"cl_text\": "); sb_putint(&b, a->cfg.cl_text);
    sb_puts(&b, ",\n  \"cl_glass\": "); sb_putint(&b, a->cfg.cl_glass);
    sb_puts(&b, ",\n  \"glass\": "); sb_putint(&b, a->cfg.glass);
    sb_puts(&b, ",\n  \"glass_a\": "); sb_putint(&b, a->cfg.glass_a);
    sb_puts(&b, "\n}\n");
    data_path(path, MAX_PATH, L"config.json");
    data_path(dir, MAX_PATH, NULL);
    ensure_dir(dir);
    ok = write_atomic(path, b.p ? b.p : "{}", b.len);
    free(b.p);
    return ok;
}

int store_load(App *a)
{
    WCHAR path[MAX_PATH];
    char *raw;
    int len = 0, i;
    JVal *root, *arr;
    data_path(path, MAX_PATH, L"todos.json");
    raw = read_file(path, &len);
    if (!raw) return 0;
    root = json_parse(raw);
    free(raw);
    if (!root) return 0;
    arr = json_get(root, L"todos");
    if (arr && arr->t == J_ARR) {
        for (i = 0; i < arr->n; i++) {
            JVal *it = arr->items[i];
            const WCHAR *t;
            Todo *td;
            if (!it || it->t != J_OBJ) continue;
            t = json_wstr(it, L"t", L"");
            if (!t[0]) continue;
            td = list_push(a);
            if (!td) break;
            wcpy(td->title, t, MAX_TITLE);
            td->done = json_int(it, L"done", 0) ? 1 : 0;
            {
                const WCHAR *ds = json_wstr(it, L"ddl", NULL);
                if (ds && parse_date_str(ds, &td->ddl)) td->has_ddl = 1;
            }
            {
                const WCHAR *ns = json_wstr(it, L"note", NULL);
                const WCHAR *is = json_wstr(it, L"img", NULL);
                if (ns && ns[0]) td->note = wdup(ns);
                if (is && is[0]) td->img = wdup(is);
            }
        }
    }
    json_free(root);
    list_build_view(a);
    return 1;
}

int cfg_load(App *a)
{
    WCHAR path[MAX_PATH];
    char *raw;
    int len = 0;
    JVal *root;
    data_path(path, MAX_PATH, L"config.json");
    raw = read_file(path, &len);
    if (!raw) return 0;
    root = json_parse(raw);
    free(raw);
    if (!root) return 0;
    a->cfg.x = json_int(root, L"x", -1);
    a->cfg.y = json_int(root, L"y", -1);
    a->cfg.width = json_int(root, L"width", 300);
    a->cfg.topmost = json_int(root, L"topmost", 1);
    a->cfg.autostart = json_int(root, L"autostart", 0);
    a->cfg.theme = json_int(root, L"theme", 0);
    a->cfg.manual_order = json_int(root, L"manual_order", 0);
    a->cfg.edge = json_int(root, L"edge", 0);
    a->cfg.peek = json_int(root, L"peek", 0);
    a->cfg.font = json_int(root, L"font", 100);
    a->cfg.cl_page = json_int(root, L"cl_page", a->cfg.cl_page);
    a->cfg.cl_bar = json_int(root, L"cl_bar", a->cfg.cl_bar);
    a->cfg.cl_text = json_int(root, L"cl_text", a->cfg.cl_text);
    a->cfg.cl_glass = json_int(root, L"cl_glass", a->cfg.cl_glass);
    a->cfg.glass = json_int(root, L"glass", 0);
    a->cfg.glass_a = json_int(root, L"glass_a", a->cfg.glass_a);
    if (a->cfg.edge < 0 || a->cfg.edge > 3) a->cfg.edge = 0;
    if (a->cfg.peek != 0 && a->cfg.peek != 1) a->cfg.peek = 0;
    if (a->cfg.font < FONT_MIN || a->cfg.font > FONT_MAX) a->cfg.font = 100;
    if (a->cfg.theme < 0 || a->cfg.theme > 2) a->cfg.theme = 0;
    if (a->cfg.glass != 0 && a->cfg.glass != 1) a->cfg.glass = 0;
    if (a->cfg.glass_a < 0) a->cfg.glass_a = 0;
    if (a->cfg.glass_a > 255) a->cfg.glass_a = 255;
    json_free(root);
    if (a->cfg.width < 220) a->cfg.width = 220;
    if (a->cfg.width > 900) a->cfg.width = 900;
    return 1;
}
