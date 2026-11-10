/* themdlg.c - 「主题」窗口：自己定页面背景 / 页边栏 / 字体颜色，外加玻璃磨砂配色。
 *
 * 整窗自绘（跟主窗口一个套路），每行一个色块，点「选择…」弹出 colorpick.c 那个
 * 取色器。改完立刻生效并落盘，所以这个窗口不需要「确定 / 取消」，只有「完成」。
 */
#include "app.h"

#define CLS_THEME L"DeskTodoTheme"

#define K_BG   RGB(0xF2, 0xEF, 0xEA)
#define K_CARD RGB(0xFF, 0xFF, 0xFF)
#define K_TXT  RGB(0x3C, 0x38, 0x34)
#define K_MUT  RGB(0x8C, 0x86, 0x7E)
#define K_BORD RGB(0xC9, 0xC3, 0xBA)

typedef struct {
    App   *app;
    HWND   hwnd;
    HFONT  font, fontb;
    int    dpi, fs;
    int    hot;                 /* 悬停命中区：0 无，1..4 色块行，5 玻璃开关，6 恢复默认，7 完成 */
} TD;

static TD T;
static int g_done;

static int tz(int v) { return MulDiv(MulDiv(v, T.dpi, 96), T.fs, 100); }

static HFONT td_font(int qpt, int bold)
{
    int q = MulDiv(qpt * 4, T.fs, 100);
    if (q < 20) q = 20;
    return CreateFontW(-MulDiv(q, T.dpi, 4 * 72), 0, 0, 0,
                       bold ? FW_SEMIBOLD : FW_NORMAL, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Microsoft YaHei UI");
}

typedef struct { int x, y, w, h; } RC;

static void rect_row(int i, RC *r)   { r->x = tz(14); r->y = tz(14 + i * 46); r->w = tz(392); r->h = tz(36); }
static void rect_sw(int i, RC *r)    { r->x = tz(150); r->y = tz(20 + i * 46); r->w = tz(60); r->h = tz(24); }
static void rect_pick(int i, RC *r)  { r->x = tz(222); r->y = tz(20 + i * 46); r->w = tz(72); r->h = tz(24); }
static void rect_glass(RC *r)        { r->x = tz(14); r->y = tz(14 + 4 * 46); r->w = tz(392); r->h = tz(34); }
static void rect_btn(int which, RC *r)
{
    if (which == 0) { r->x = tz(214); r->y = tz(266); r->w = tz(92); r->h = tz(32); }
    else            { r->x = tz(314); r->y = tz(266); r->w = tz(92); r->h = tz(32); }
}

static int in_rc(RC *r, int x, int y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static COLORREF cl_of(int v) { return RGB((v >> 16) & 255, (v >> 8) & 255, v & 255); }

static void draw_text(HDC dc, int x, int y, int w, int h, const WCHAR *s, HFONT f, COLORREF col, UINT fmt)
{
    RECT r;
    HGDIOBJ of;
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, col);
    of = SelectObject(dc, f);
    DrawTextW(dc, s, -1, &r, fmt | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, of);
}

static void draw_box(HDC dc, RC *r, COLORREF fill, COLORREF bord, int round)
{
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, bord);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    if (round) RoundRect(dc, r->x, r->y, r->x + r->w, r->y + r->h, tz(8), tz(8));
    else       Rectangle(dc, r->x, r->y, r->x + r->w, r->y + r->h);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

static void on_paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT cr;
    RC r;
    int i;
    static const WCHAR *NM[4] = { L"页面背景", L"页边栏", L"字体颜色", L"玻璃配色" };
    int *val[4];

    GetClientRect(hwnd, &cr);
    {
        HBRUSH b = CreateSolidBrush(K_BG);
        FillRect(dc, &cr, b);
        DeleteObject(b);
    }

    val[0] = &T.app->cfg.cl_page;
    val[1] = &T.app->cfg.cl_bar;
    val[2] = &T.app->cfg.cl_text;
    val[3] = &T.app->cfg.cl_glass;

    for (i = 0; i < 4; i++) {
        rect_row(i, &r);
        if (T.hot == i + 1) draw_box(dc, &r, RGB(0xEA, 0xE5, 0xDD), RGB(0xEA, 0xE5, 0xDD), 1);
        draw_text(dc, r.x + tz(10), r.y, tz(130), r.h, NM[i], T.font, K_TXT, DT_LEFT);

        rect_sw(i, &r);
        draw_box(dc, &r, cl_of(*val[i]), K_BORD, 1);
        rect_pick(i, &r);
        draw_box(dc, &r, T.hot == i + 1 ? RGB(0xFF, 0xFF, 0xFF) : K_CARD, K_BORD, 1);
        draw_text(dc, r.x, r.y, r.w, r.h, L"选择…", T.font, K_TXT, DT_CENTER);
    }

    /* 玻璃开关 */
    rect_glass(&r);
    {
        RC cb;
        cb.x = r.x + tz(10); cb.y = r.y + tz(8); cb.w = tz(18); cb.h = tz(18);
        draw_box(dc, &cb, T.app->cfg.glass ? RGB(0x4C, 0x8D, 0xFF) : K_CARD, K_BORD, 0);
        if (T.app->cfg.glass) {
            HPEN wp = CreatePen(PS_SOLID, tz(2), RGB(255, 255, 255));
            HGDIOBJ op = SelectObject(dc, wp), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
            MoveToEx(dc, cb.x + cb.w / 4, cb.y + cb.h / 2, NULL);
            LineTo(dc, cb.x + cb.w / 2, cb.y + cb.h * 3 / 4);
            LineTo(dc, cb.x + cb.w - cb.w / 5, cb.y + cb.h / 4);
            SelectObject(dc, op);
            SelectObject(dc, ob);
            DeleteObject(wp);
        }
        draw_text(dc, cb.x + cb.w + tz(10), r.y, tz(330), r.h,
                  L"启用玻璃磨砂（Win10/11 亚克力模糊，透明度用 A 通道）",
                  T.font, T.hot == 5 ? K_TXT : K_MUT, DT_LEFT);
    }

    /* 提示 */
    draw_text(dc, tz(14), tz(226), tz(392), tz(24),
              L"改动立即生效并保存；不想自己配就点「恢复默认」。", T.font, K_MUT, DT_LEFT);

    rect_btn(0, &r);
    draw_box(dc, &r, T.hot == 6 ? RGB(0xE3, 0xDE, 0xD6) : K_CARD, K_BORD, 1);
    draw_text(dc, r.x, r.y, r.w, r.h, L"恢复默认", T.font, K_TXT, DT_CENTER);

    rect_btn(1, &r);
    draw_box(dc, &r, T.hot == 7 ? RGB(0x4C, 0x8D, 0xFF) : RGB(0x3E, 0x74, 0xD0), K_BORD, 1);
    draw_text(dc, r.x, r.y, r.w, r.h, L"完成", T.fontb, RGB(255, 255, 255), DT_CENTER);

    EndPaint(hwnd, &ps);
}

/* 应用改动：重建调色板 + 玻璃效果 + 立刻落盘 + 两窗一起重画 */
static void apply_live(App *a)
{
    a->cfg.theme = 2;
    theme_init(a);
    theme_apply_glass(a);
    cfg_save(a);
    widget_fit(a);
    InvalidateRect(a->hwnd, NULL, FALSE);
    if (T.hwnd) InvalidateRect(T.hwnd, NULL, FALSE);
}

static int hit_test(int x, int y)
{
    RC r;
    int i;
    for (i = 0; i < 4; i++) {
        rect_pick(i, &r);
        if (in_rc(&r, x, y)) return i + 1;
        rect_sw(i, &r);
        if (in_rc(&r, x, y)) return i + 1;
    }
    rect_glass(&r);
    if (in_rc(&r, x, y)) return 5;
    rect_btn(0, &r);
    if (in_rc(&r, x, y)) return 6;
    rect_btn(1, &r);
    if (in_rc(&r, x, y)) return 7;
    return 0;
}

static void on_click(App *a, int hit)
{
    int *val[4];
    int alpha;
    static const WCHAR *TT[4] = { L"页面背景色", L"页边栏颜色", L"字体颜色", L"玻璃配色" };
    val[0] = &a->cfg.cl_page;
    val[1] = &a->cfg.cl_bar;
    val[2] = &a->cfg.cl_text;
    val[3] = &a->cfg.cl_glass;

    if (hit >= 1 && hit <= 4) {
        int rgb = *val[hit - 1];
        alpha = a->cfg.glass_a;
        if (hit == 4) {
            if (colorpick_open(a, TT[hit - 1], &rgb, &alpha)) {
                *val[hit - 1] = rgb;
                a->cfg.glass_a = alpha;
                apply_live(a);
            }
        } else if (colorpick_open(a, TT[hit - 1], &rgb, NULL)) {
            *val[hit - 1] = rgb;
            apply_live(a);
        }
    } else if (hit == 5) {
        a->cfg.glass = !a->cfg.glass;
        apply_live(a);
    } else if (hit == 6) {
        a->cfg.theme = 0;
        a->cfg.glass = 0;
        theme_defaults(a);
        theme_init(a);
        theme_apply_glass(a);
        cfg_save(a);
        widget_fit(a);
        InvalidateRect(a->hwnd, NULL, FALSE);
        InvalidateRect(T.hwnd, NULL, FALSE);
    } else if (hit == 7) {
        g_done = 1;
        DestroyWindow(T.hwnd);
    }
}

static LRESULT CALLBACK TdProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    switch (msg) {
    case WM_CREATE:
        T.hwnd = hwnd;
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        on_paint(hwnd);
        return 0;
    case WM_MOUSEMOVE: {
        int h = hit_test(x, y);
        if (h != T.hot) {
            T.hot = h;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        {
            TRACKMOUSEEVENT tme;
            ZeroMemory(&tme, sizeof(tme));
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        T.hot = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        on_click(T.app, hit_test(x, y));
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_RETURN) { g_done = 1; DestroyWindow(hwnd); }
        return 0;
    case WM_CLOSE:
        g_done = 1;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_done = 1;
        if (T.font)  { DeleteObject(T.font);  T.font = NULL; }
        if (T.fontb) { DeleteObject(T.fontb); T.fontb = NULL; }
        T.hwnd = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void theme_dlg_destroy(void)
{
    if (T.hwnd) DestroyWindow(T.hwnd);
}

void theme_dlg_open(App *a)
{
    WNDCLASSEXW wc;
    MSG msg;
    RECT ar, wr, wa;
    int ww, wh, x, y;

    if (!a) return;
    if (T.hwnd) { SetForegroundWindow(T.hwnd); return; }

    memset(&T, 0, sizeof(T));
    T.app = a;
    T.dpi = a->dpi > 0 ? a->dpi : 96;
    T.fs  = a->cfg.font;
    T.font  = td_font(9, 0);
    T.fontb = td_font(9, 1);

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TdProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = a->hIcon;
    wc.lpszClassName = CLS_THEME;
    RegisterClassExW(&wc);

    ar.left = 0; ar.top = 0; ar.right = tz(420); ar.bottom = tz(312);
    AdjustWindowRectEx(&ar, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOOLWINDOW);
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
    T.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, CLS_THEME, L"主题",
                             WS_POPUP | WS_CAPTION | WS_SYSMENU,
                             x, y, ww, wh, a->hwnd, NULL, GetModuleHandleW(NULL), NULL);
    if (!T.hwnd) return;
    ShowWindow(T.hwnd, SW_SHOW);
    UpdateWindow(T.hwnd);
    SetForegroundWindow(T.hwnd);

    while (!g_done && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) { PostQuitMessage((int)msg.wParam); break; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (T.hwnd) DestroyWindow(T.hwnd);
    T.app = NULL;
    InvalidateRect(a->hwnd, NULL, FALSE);
}
