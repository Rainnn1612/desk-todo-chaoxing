/* courses.c - 「筛选课程」窗口：手动维护学习通的旧课退休名单。
 *
 * 退休名单是纯文本 %APPDATA%\DeskTodo\sync_retired.txt，一行一门课：
 *     cid_classid <TAB> 课程名 <TAB> 说明
 * 说明列写日期或「无作业」的是同步器自动判定的；写「手动」的是这个窗口加进去的。
 * 手动项会越过「当前学期永不退休」的保护 —— 用户自己说了算。
 *
 * 课程清单来自同步器每次跑完写的 courses.json，所以得先同步过一次，这个窗口才有内容。
 * 这里只做筛选，不发任何网络请求。
 */
#include "app.h"

#define CLS_COURSES L"DeskTodoCourses"

#define ID_LIST    3001
#define ID_SAVE    3002
#define ID_CANCELB 3003
#define ID_INFO    3004

#define MAXC 160

typedef struct {
    App   *app;
    HWND   hwnd, h_list, h_save, h_cancel, h_info;
    HFONT  font, fontb;
    HBRUSH br_page;
    int    dpi, fs;
    int    n;                      /* 课程门数 */
    WCHAR  name[MAXC][128];
    WCHAR  key[MAXC][40];
    int    cur[MAXC];              /* 1 = 当前学期 */
    int    was[MAXC];              /* 1 = 读进来时已在退休名单里 */
} Courses;

static Courses C;
static int g_done;

static int fzd(int v)
{
    return MulDiv(MulDiv(v, C.dpi, 96), C.fs, 100);
}

static HFONT mk_font(int qpt, int bold)
{
    int q = MulDiv(qpt * 4, C.fs, 100);
    if (q < 20) q = 20;
    return CreateFontW(-MulDiv(q, C.dpi, 4 * 72), 0, 0, 0,
                       bold ? FW_SEMIBOLD : FW_NORMAL, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Microsoft YaHei UI");
}

static HWND mk(const WCHAR *cls, const WCHAR *txt, DWORD st, DWORD ex,
               int x, int y, int w, int h, int id, HFONT f)
{
    HWND r = CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | st,
                             fzd(x), fzd(y), fzd(w), fzd(h),
                             C.hwnd, (HMENU)(INT_PTR)id, NULL, NULL);
    if (r && f) SendMessageW(r, WM_SETFONT, (WPARAM)f, TRUE);
    return r;
}

static void free_res(void)
{
    if (C.font)   { DeleteObject(C.font);   C.font = NULL; }
    if (C.fontb)  { DeleteObject(C.fontb);  C.fontb = NULL; }
    if (C.br_page){ DeleteObject(C.br_page);C.br_page = NULL; }
}

/* ---------------- 读课程清单 ---------------- */

static int load_courses(App *a)
{
    WCHAR path[MAX_PATH];
    char *raw;
    int len = 0, n = 0, i;
    JVal *root, *arr;

    data_path(path, MAX_PATH, L"courses.json");
    raw = read_file(path, &len);
    if (!raw) return 0;
    root = json_parse(raw);
    free(raw);
    if (!root) return 0;
    arr = json_get(root, L"courses");
    if (arr && arr->t == J_ARR) {
        for (i = 0; i < arr->n && n < MAXC; i++) {
            JVal *o = arr->items[i];
            const WCHAR *k, *nm;
            if (!o || o->t != J_OBJ) continue;
            k  = json_wstr(o, L"key", L"");
            nm = json_wstr(o, L"name", L"");
            if (!k[0]) continue;
            wcpy(C.key[n], k, 40);
            if (nm[0]) wcpy(C.name[n], nm, 128);
            else       wcpy(C.name[n], k, 128);
            C.cur[n] = json_int(o, L"cur", 0);
            C.was[n] = json_int(o, L"retired", 0);
            n++;
        }
    }
    json_free(root);
    (void)a;
    return n;
}

/* ---------------- 保存退休名单 ---------------- */

typedef struct { WCHAR *p; int len, cap; } WB;

static void wb_put(WB *b, const WCHAR *s)
{
    int i, add = 0, nc;
    WCHAR *np;
    while (s[add]) add++;
    if (b->len + add + 2 > b->cap) {
        nc = b->cap ? b->cap : 4096;
        while (nc < b->len + add + 2) nc *= 2;
        np = (WCHAR *)realloc(b->p, (size_t)nc * sizeof(WCHAR));
        if (!np) return;
        b->p = np;
        b->cap = nc;
    }
    for (i = 0; i < add; i++) b->p[b->len++] = s[i];
    b->p[b->len] = 0;
}

static void wb_ch(WB *b, WCHAR c)
{
    WCHAR t[2];
    t[0] = c;
    t[1] = 0;
    wb_put(b, t);
}

/* 取一行的主键（第一个 TAB 之前那一段）；空行返回 0 */
static int line_key(const WCHAR *ln, WCHAR *out, int n)
{
    int i = 0;
    if (!ln) return 0;
    while (ln[i] && ln[i] != L'\t' && i < n - 1) { out[i] = ln[i]; i++; }
    out[i] = 0;
    return i > 0;
}

static int course_index(const WCHAR *k)
{
    int i;
    for (i = 0; i < C.n; i++)
        if (!_wcsicmp(C.key[i], k)) return i;
    return -1;
}

static void on_save(void)
{
    WCHAR path[MAX_PATH], kpath[MAX_PATH];
    char *raw, *u8;
    WCHAR *old = NULL;
    WCHAR **lines = NULL;
    int  oldn = 0, nlines = 0, i, j, sel, hit;
    WB b, kb;

    data_path(path, MAX_PATH, L"sync_retired.txt");
    data_path(kpath, MAX_PATH, L"sync_keep.txt");
    memset(&b, 0, sizeof(b));
    memset(&kb, 0, sizeof(kb));

    raw = read_file(path, &oldn);
    if (raw) {
        old = u82w(raw);
        free(raw);
    }
    if (old) {
        lines = (WCHAR **)calloc((size_t)wlen(old) + 4, sizeof(WCHAR *));
        if (lines) {
            lines[nlines++] = old;
            for (i = 0; old[i]; i++) {
                if (old[i] == L'\r' || old[i] == L'\n') {
                    old[i] = 0;
                    if (old[i + 1] && old[i + 1] != L'\r' && old[i + 1] != L'\n')
                        lines[nlines++] = old + i + 1;
                }
            }
        }
    }

    /* 勾上的课：旧文件里有就原样保留那一行（保住同步器写进去的日期），没有就新建一行写「手动」 */
    for (i = 0; i < C.n; i++) {
        sel = (int)SendMessageW(C.h_list, LB_GETSEL, (WPARAM)i, 0) > 0;
        if (!sel) {
            /* 取消勾选 = 我要同步它：记进 sync_keep.txt，同步器以后不会再自动把它退休 */
            wb_put(&kb, C.key[i]);
            wb_ch(&kb, L'\t');
            wb_put(&kb, C.name[i]);
            wb_ch(&kb, L'\r');
            wb_ch(&kb, L'\n');
            continue;
        }
        hit = -1;
        for (j = 0; j < nlines; j++) {
            WCHAR k[64];
            if (line_key(lines[j], k, 64) && !_wcsicmp(k, C.key[i])) { hit = j; break; }
        }
        if (hit >= 0 && lines) {
            wb_put(&b, lines[hit]);
        } else {
            wb_put(&b, C.key[i]);
            wb_ch(&b, L'\t');
            wb_put(&b, C.name[i]);
            wb_ch(&b, L'\t');
            wb_put(&b, L"手动");
        }
        wb_ch(&b, L'\r');
        wb_ch(&b, L'\n');
    }

    /* 旧文件里那些这次没出现在课程清单里的行，原样留着，别把用户数据弄丢 */
    for (j = 0; j < nlines; j++) {
        WCHAR k[64];
        if (!line_key(lines[j], k, 64)) continue;
        if (course_index(k) >= 0) continue;
        wb_put(&b, lines[j]);
        wb_ch(&b, L'\r');
        wb_ch(&b, L'\n');
    }

    /* sync_keep.txt 是整份重写的：勾上的课不会出现在里面（等于解除了「保留」），
       取消勾选的课才会写进去。清单里已经没有的老条目下次同步自然就失效了 —— 无所谓。 */

    if (b.p) {
        u8 = w2u8(b.p);
        if (u8) {
            int ln = 0;
            while (u8[ln]) ln++;
            write_atomic(path, u8, ln);
            free(u8);
        }
        free(b.p);
    }
    u8 = kb.p ? w2u8(kb.p) : NULL;
    if (u8) {
        int ln = 0;
        while (u8[ln]) ln++;
        write_atomic(kpath, u8, ln);
        free(u8);
    }
    if (kb.p) free(kb.p);
    if (lines) free(lines);
    if (old) free(old);
}

static void upd_info(void)
{
    WCHAR t[192];
    int i, sel = 0;
    for (i = 0; i < C.n; i++)
        if ((int)SendMessageW(C.h_list, LB_GETSEL, (WPARAM)i, 0) > 0) sel++;
    wcpy(t, L"共 ", 192);
    wcat_int(t, C.n, 192);
    wcat(t, L" 门，勾选 ", 192);
    wcat_int(t, sel, 192);
    wcat(t, L" 门（勾上 = 以后不再同步这门课）", 192);
    if (C.h_info) SetWindowTextW(C.h_info, t);
}

/* ---------------- 窗口过程 ---------------- */

static LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        /* 子控件的父窗口要用这个 hwnd：此时 C.hwnd 还没被外面赋值，先在这里补上 */
        C.hwnd = hwnd;
        mk(L"STATIC", L"勾选要跳过的课程，以后同步时不再请求它们。",
           0, 0, 14, 10, 442, 22, 0, C.font);
        C.h_list = mk(L"LISTBOX", NULL,
                      WS_VSCROLL | WS_BORDER | WS_TABSTOP | LBS_NOINTEGRALHEIGHT |
                      LBS_NOTIFY | LBS_MULTIPLESEL,
                      WS_EX_CLIENTEDGE, 14, 36, 442, 396, ID_LIST, C.font);
        C.h_info = mk(L"STATIC", L"", 0, 0, 14, 440, 442, 20, ID_INFO, C.font);
        C.h_save   = mk(L"BUTTON", L"保存", WS_TABSTOP | BS_DEFPUSHBUTTON, 0,
                        348, 470, 100, 32, ID_SAVE, C.fontb);
        C.h_cancel = mk(L"BUTTON", L"取消", WS_TABSTOP, 0,
                        238, 470, 100, 32, ID_CANCELB, C.font);
        if (C.h_list) {
            int i;
            for (i = 0; i < C.n; i++) {
                WCHAR t[220];
                wcpy(t, C.name[i], 220);
                if (C.cur[i]) wcat(t, L"    · 当前学期", 220);
                if (C.was[i]) wcat(t, L"    · 已跳过", 220);
                SendMessageW(C.h_list, LB_ADDSTRING, 0, (LPARAM)t);
                if (C.was[i])
                    SendMessageW(C.h_list, LB_SETSEL, (WPARAM)TRUE, (LPARAM)i);
            }
            /* LB_SETSEL 会把勾中的项滚进视野，最后一行往往在底部，于是首行被顶出去了；
               这里强制把列表拉回顶部，打开时从头开始看 */
            SendMessageW(C.h_list, LB_SETTOPINDEX, 0, 0);
        }
        upd_info();
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, RGB(0x4A, 0x46, 0x42));
        return (LRESULT)C.br_page;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT cr;
        GetClientRect(hwnd, &cr);
        if (C.br_page) FillRect(dc, &cr, C.br_page);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_SAVE:
            if (HIWORD(wp) == BN_CLICKED) { on_save(); DestroyWindow(hwnd); }
            return 0;
        case IDOK:
            /* IsDialogMessageW 处理回车时合成的是 IDOK(1) */
            on_save();
            DestroyWindow(hwnd);
            return 0;
        case ID_CANCELB:
        case IDCANCEL:
            DestroyWindow(hwnd);
            return 0;
        case ID_LIST:
            if (HIWORD(wp) == LBN_SELCHANGE) upd_info();
            return 0;
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_done = 1;
        free_res();
        C.hwnd = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void courses_destroy(void)
{
    if (C.hwnd) { DestroyWindow(C.hwnd); return; }
    free_res();
}

void courses_open(App *a)
{
    WNDCLASSEXW wc;
    MSG msg;
    RECT wr, wa, ar;
    int cw, ch, ww, wh, x, y;
    DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;

    if (!a) return;
    if (C.hwnd) { SetForegroundWindow(C.hwnd); return; }

    memset(&C, 0, sizeof(C));
    C.app = a;
    C.dpi = a->dpi > 0 ? a->dpi : 96;
    C.fs  = a->cfg.font;
    C.font   = mk_font(9, 0);
    C.fontb  = mk_font(9, 1);
    C.br_page = CreateSolidBrush(RGB(0xF7, 0xF5, 0xF2));
    C.n = load_courses(a);
    if (C.n == 0) {
        MessageBoxW(a->hwnd,
                    L"还没有课程清单。\n\n"
                    L"先点一次「立即同步学习通」，同步器会把识别到的课程写进 courses.json，"
                    L"再回来这里筛选。",
                    L"筛选课程", MB_ICONINFORMATION | MB_OK);
        free_res();
        C.app = NULL;
        return;
    }

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DlgProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = a->hIcon;
    wc.lpszClassName = CLS_COURSES;
    RegisterClassExW(&wc);

    cw = fzd(470);
    ch = fzd(520);
    ar.left = 0; ar.top = 0; ar.right = cw; ar.bottom = ch;
    AdjustWindowRectEx(&ar, st, FALSE, WS_EX_TOOLWINDOW);
    ww = ar.right - ar.left;
    wh = ar.bottom - ar.top;

    widget_work_area(a->hwnd, &wa);
    GetWindowRect(a->hwnd, &wr);
    x = wr.left + ((wr.right - wr.left) - ww) / 2;
    y = wr.top + ((wr.bottom - wr.top) - wh) / 2;
    if (x + ww > wa.right) x = wa.right - ww;
    if (x < wa.left) x = wa.left;
    if (y + wh > wa.bottom) y = wa.bottom - wh;
    if (y < wa.top) y = wa.top;

    g_done = 0;
    C.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT | WS_EX_TOPMOST,
                             CLS_COURSES, L"筛选课程", st,
                             x, y, ww, wh, a->hwnd, NULL,
                             GetModuleHandleW(NULL), NULL);
    if (!C.hwnd) {
        free_res();
        C.app = NULL;
        return;
    }

    EnableWindow(a->hwnd, FALSE);
    ShowWindow(C.hwnd, SW_SHOW);
    UpdateWindow(C.hwnd);
    SetForegroundWindow(C.hwnd);
    if (C.h_list) SetFocus(C.h_list);

    while (!g_done && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (C.hwnd && IsDialogMessageW(C.hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (C.hwnd) DestroyWindow(C.hwnd);
    free_res();
    C.app = NULL;
    EnableWindow(a->hwnd, TRUE);
    InvalidateRect(a->hwnd, NULL, FALSE);
}
