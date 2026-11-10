/* widget.c - 窗口类、无边框窗口、拖动、贴边收起、托盘、全局热键 */
#include "app.h"

#define CLS_NAME L"DeskTodoWnd"
#define PEEK_W   5

#define MENU_T_SHOW   2001
#define MENU_T_TOP    2002
#define MENU_T_PEEK   2003
#define MENU_T_AUTO   2004
#define MENU_T_QUIT   2005

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

static void get_work(HWND hwnd, RECT *wa)
{
    MONITORINFO mi;
    mi.cbSize = sizeof(MONITORINFO);
    if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        *wa = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, wa, 0);
    }
}

/* 默认位置：主屏右上角，留 24px 边距 */
static void default_pos(App *a, RECT *wa, int w, int h)
{
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    wa->left = 0; wa->top = 0; wa->right = sw; wa->bottom = sh;
    a->cfg.x = sw - w - sc(a, 24);
    a->cfg.y = sc(a, 60);
    if (a->cfg.y + h > sh) a->cfg.y = sh - h - sc(a, 10);
}

static int pos_visible(int x, int y, int w, int h)
{
    /* 简化：只要与虚拟屏幕有一块重叠即可 */
    {
        RECT v;
        v.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        v.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        v.right = v.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        v.bottom = v.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (x + w < v.left + 40) return 0;
        if (x > v.right - 40) return 0;
        if (y + h < v.top + 10) return 0;
        if (y > v.bottom - 10) return 0;
    }
    return 1;
}

void widget_apply_region(App *a)
{
    HRGN r;
    if (a->W <= 0 || a->H <= 0) return;
    r = CreateRoundRectRgn(0, 0, a->W + 1, a->H + 1, sc(a, 12), sc(a, 12));
    if (r) SetWindowRgn(a->hwnd, r, TRUE);
}

void widget_fit(App *a)
{
    RECT wr;
    int nh, ww, ch, vph, maxs;

    ch = ui_content_h(a);
    vph = ui_viewport_h(a);
    maxs = ch - vph;
    if (maxs < 0) maxs = 0;
    if (a->scroll > maxs) a->scroll = maxs;
    if (a->scroll < 0) a->scroll = 0;

    nh = ui_needed_height(a);
    ww = fz(a, a->cfg.width);
    GetWindowRect(a->hwnd, &wr);
    if ((wr.right - wr.left) != ww || (wr.bottom - wr.top) != nh) {
        SetWindowPos(a->hwnd, NULL, 0, 0, ww, nh,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void widget_save_pos(App *a)
{
    RECT wr, wa;
    GetWindowRect(a->hwnd, &wr);
    get_work(a->hwnd, &wa);
    if (a->cfg.peek && a->cfg.edge == 1) {
        a->cfg.x = wa.left;
        a->cfg.y = wr.top;
    } else if (a->cfg.peek && a->cfg.edge == 2) {
        a->cfg.x = wa.right - (wr.right - wr.left);
        a->cfg.y = wr.top;
    } else if (a->cfg.peek && a->cfg.edge == 3) {
        a->cfg.x = wr.left;      /* 顶边收起只动 y，x 就是拖动后的真实位置 */
        a->cfg.y = wa.top;
    } else {
        a->cfg.x = wr.left;
        a->cfg.y = wr.top;
    }
    cfg_save(a);
}

void widget_slide_to(App *a, int tx, int ty)
{
    RECT wr;
    GetWindowRect(a->hwnd, &wr);
    a->anim_tx = tx;
    a->anim_ty = ty;
    if (wr.left == tx && wr.top == ty) { a->animating = 0; return; }
    a->animating = 1;
    SetTimer(a->hwnd, T_ANIM, 10, NULL);
}

static int step_toward(int cur, int target, int minstep)
{
    int d = target - cur, s;
    if (d == 0) return cur;
    s = abs(d) / 4 + 1;
    if (s < minstep) s = minstep;
    if (s > abs(d)) s = abs(d);
    return cur + (d > 0 ? s : -s);
}

static void do_anim(App *a)
{
    RECT wr;
    int x, y;
    GetWindowRect(a->hwnd, &wr);
    if (wr.left == a->anim_tx && wr.top == a->anim_ty) {
        KillTimer(a->hwnd, T_ANIM);
        a->animating = 0;
        return;
    }
    x = step_toward(wr.left, a->anim_tx, sc(a, 3));
    y = step_toward(wr.top,  a->anim_ty, sc(a, 3));
    SetWindowPos(a->hwnd, NULL, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* 展开位置：贴哪条边就对齐那条边的内侧，另一轴保持不动 */
static void revealed_pos(App *a, int *px, int *py)
{
    RECT wa, wr;
    get_work(a->hwnd, &wa);
    GetWindowRect(a->hwnd, &wr);
    *px = wr.left;
    *py = wr.top;
    if (a->cfg.edge == 1)      *px = wa.left;
    else if (a->cfg.edge == 2) *px = wa.right - (wr.right - wr.left);
    else if (a->cfg.edge == 3) *py = wa.top;
}

/* 收起位置：只留 PEEK_W 像素的细条贴在工作区边上 */
void widget_peek_pos(App *a, int *px, int *py)
{
    RECT wa, wr;
    get_work(a->hwnd, &wa);
    GetWindowRect(a->hwnd, &wr);
    *px = wr.left;
    *py = wr.top;
    if (a->cfg.edge == 1)      *px = wa.left - (wr.right - wr.left) + sc(a, PEEK_W);
    else if (a->cfg.edge == 2) *px = wa.right - sc(a, PEEK_W);
    else if (a->cfg.edge == 3) *py = wa.top - (wr.bottom - wr.top) + sc(a, PEEK_W);
}

void widget_work_area(HWND hwnd, RECT *r) { get_work(hwnd, r); }

void widget_check_edge(App *a)
{
    RECT wa, wr;
    int w, tx, ty;
    if (!a->cfg.edge) {
        /* 关闭贴边隐藏时，若窗口还停在收起的半出屏位置，把它拉回工作区 */
        a->cfg.peek = 0;
        get_work(a->hwnd, &wa);
        GetWindowRect(a->hwnd, &wr);
        w = wr.right - wr.left;
        if (wr.right > wa.right) {
            SetWindowPos(a->hwnd, NULL, wa.right - w, wr.top, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            widget_save_pos(a);
        } else if (wr.left < wa.left) {
            SetWindowPos(a->hwnd, NULL, wa.left, wr.top, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            widget_save_pos(a);
        } else if (wr.top < wa.top) {
            SetWindowPos(a->hwnd, NULL, wr.left, wa.top, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            widget_save_pos(a);
        }
        return;
    }
    /* 开启时：先对齐到该边，然后立刻收起一次（给出可见的反馈） */
    get_work(a->hwnd, &wa);
    GetWindowRect(a->hwnd, &wr);
    w = wr.right - wr.left;
    if (a->cfg.edge == 1)      SetWindowPos(a->hwnd, NULL, wa.left, wr.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    else if (a->cfg.edge == 2) SetWindowPos(a->hwnd, NULL, wa.right - w, wr.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    else if (a->cfg.edge == 3) SetWindowPos(a->hwnd, NULL, wr.left, wa.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    a->cfg.peek = 1;
    widget_peek_pos(a, &tx, &ty);
    widget_slide_to(a, tx, ty);
    widget_save_pos(a);   /* peek=1 → 存贴边坐标，而不是动画途中的位置 */
}

/* 拖动结束后调用 */
static void after_move(App *a)
{
    RECT wr, wa, cr;
    int nw, marg;

    GetClientRect(a->hwnd, &cr);
    if (cr.right > 0) {
        /* 像素宽 → 逻辑宽：要同时除掉 DPI 和字号缩放，否则放大字号后
           拖一次宽度，逻辑宽度就被写成了放大后的像素数 */
        nw = MulDiv(MulDiv(cr.right, 96, a->dpi), 100, a->cfg.font);
        if (nw < 220) nw = 220;
        if (nw > 900) nw = 900;
        a->cfg.width = nw;
    }
    widget_fit(a);

    /* 松手时若贴着某条工作区边 → 记住那条边并收起；否则就摊在桌面上 */
    get_work(a->hwnd, &wa);
    GetWindowRect(a->hwnd, &wr);
    marg = sc(a, 6);
    if (wr.top <= wa.top + marg)           a->cfg.edge = 3;
    else if (wr.left <= wa.left + marg)    a->cfg.edge = 1;
    else if (wr.right >= wa.right - marg)  a->cfg.edge = 2;
    else {
        a->cfg.edge = 0;
        a->cfg.peek = 0;
        widget_save_pos(a);
        return;
    }
    a->cfg.peek = 1;
    {
        int tx, ty;
        widget_peek_pos(a, &tx, &ty);
        widget_slide_to(a, tx, ty);
    }
    widget_save_pos(a);
}

void widget_set_topmost(App *a, int on)
{
    SetWindowPos(a->hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void widget_tray_add(App *a)
{
    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = a->hwnd;
    nid.uID = ID_TRAY;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = a->hIconSm ? a->hIconSm : LoadIconW(NULL, IDI_APPLICATION);
    wcpy(nid.szTip, APP_NAME, 64);
    if (Shell_NotifyIconW(NIM_ADD, &nid)) a->tray_added = 1;
}

void widget_tray_remove(App *a)
{
    NOTIFYICONDATAW nid;
    if (!a->tray_added) return;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = a->hwnd;
    nid.uID = ID_TRAY;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    a->tray_added = 0;
}

void widget_show(App *a, int show)
{
    if (show) {
        if (a->cfg.peek) {
            int rx, ry;
            a->cfg.peek = 0;
            revealed_pos(a, &rx, &ry);
            SetWindowPos(a->hwnd, NULL, rx, ry, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            widget_save_pos(a);
        }
        ShowWindow(a->hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(a->hwnd, a->cfg.topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        InvalidateRect(a->hwnd, NULL, FALSE);
    } else {
        ShowWindow(a->hwnd, SW_HIDE);
    }
}

void widget_hide_to_tray(App *a)
{
    input_cancel(a);
    widget_show(a, 0);
}

void widget_quit(App *a)
{
    a->quit = 1;
    widget_save_pos(a);
    widget_tray_remove(a);
    DestroyWindow(a->hwnd);
}

void widget_set_caret(App *a)
{
    POINT p;
    if (a->editing < 0) return;
    p = ui_caret_pos(a);
    if (!(GetFocus() == a->hwnd)) SetFocus(a->hwnd);
    CreateCaret(a->hwnd, NULL, 1, a->row_h - sc(a, 12));
    SetCaretPos(p.x, p.y);
    ShowCaret(a->hwnd);
}

/* ---------------- 托盘菜单 ---------------- */

static void tray_menu(App *a)
{
    HMENU m = CreatePopupMenu();
    POINT pt;
    UINT cmd;
    int visible = IsWindowVisible(a->hwnd);

    AppendMenuW(m, MF_STRING, MENU_T_SHOW, visible ? L"隐藏" : L"显示");
    AppendMenuW(m, MF_STRING | (a->cfg.topmost ? MF_CHECKED : 0), MENU_T_TOP, L"总在最前");
    AppendMenuW(m, MF_STRING | (a->cfg.edge ? MF_CHECKED : 0), MENU_T_PEEK, L"贴边自动隐藏");
    AppendMenuW(m, MF_STRING | (a->cfg.autostart ? MF_CHECKED : 0), MENU_T_AUTO, L"开机自动启动");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, MENU_T_QUIT, L"退出");

    GetCursorPos(&pt);
    SetForegroundWindow(a->hwnd);
    cmd = (UINT)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, a->hwnd, NULL);
    DestroyMenu(m);
    PostMessageW(a->hwnd, WM_NULL, 0, 0);

    switch (cmd) {
    case MENU_T_SHOW:
        if (visible) widget_hide_to_tray(a);
        else { widget_show(a, 1); SetForegroundWindow(a->hwnd); }
        break;
    case MENU_T_TOP:
        a->cfg.topmost = !a->cfg.topmost;
        widget_set_topmost(a, a->cfg.topmost);
        cfg_save(a);
        break;
    case MENU_T_PEEK:
        a->cfg.edge = a->cfg.edge ? 0 : 2;
        a->cfg.peek = 0;
        cfg_save(a);
        widget_check_edge(a);
        break;
    case MENU_T_AUTO:
        a->cfg.autostart = !a->cfg.autostart;
        autostart_set(a->cfg.autostart);
        cfg_save(a);
        break;
    case MENU_T_QUIT:
        widget_quit(a);
        break;
    default:
        break;
    }
}

/* ---------------- 创建 ---------------- */

/* 贴边且处于展开状态时，按当前窗口大小重新对齐到那条边。
   改字号会让窗口变高变宽，贴在右边/顶边时必须重新贴回去。 */
void widget_realign(App *a)
{
    RECT wa, wr;
    int x, y, w;

    if (!a->cfg.edge) return;
    if (a->cfg.peek) {
        /* 收起状态也要重摆：字号一变窗口高度就变了，旧的收起位会把大半个窗口露出来 */
        int px, py;
        widget_peek_pos(a, &px, &py);
        SetWindowPos(a->hwnd, NULL, px, py, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }
    widget_work_area(a->hwnd, &wa);
    GetWindowRect(a->hwnd, &wr);
    w = wr.right - wr.left;
    x = wr.left;
    y = wr.top;
    if (a->cfg.edge == 1)      x = wa.left;
    else if (a->cfg.edge == 2) x = wa.right - w;
    else if (a->cfg.edge == 3) y = wa.top;
    if (x != wr.left || y != wr.top)
        SetWindowPos(a->hwnd, NULL, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

int widget_create(App *a, HINSTANCE hi)
{
    WNDCLASSEXW wc;
    RECT wa, wr;
    int w, h;

    a->dpi = 96;
    ui_metrics(a);
    w = fz(a, a->cfg.width);

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = CLS_NAME;

    a->hIcon = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                 sc(a, 32), sc(a, 32), LR_DEFAULTCOLOR);
    if (!a->hIcon)
        a->hIcon = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                     0, 0, LR_DEFAULTSIZE | LR_SHARED);
    if (!a->hIcon) a->hIcon = LoadIconW(NULL, IDI_APPLICATION);
    a->hIconSm = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (!a->hIconSm) a->hIconSm = a->hIcon;

    wc.hIcon = a->hIcon;
    wc.hIconSm = a->hIconSm;
    if (!RegisterClassExW(&wc)) return 0;

    get_work(NULL, &wa);
    h = sc(a, 30) + sc(a, 32) + sc(a, 2);
    if (a->cfg.x < 0 || a->cfg.y < 0) default_pos(a, &wa, w, h);
    else if (!pos_visible(a->cfg.x, a->cfg.y, w, h)) default_pos(a, &wa, w, h);

    wr.left = a->cfg.x; wr.top = a->cfg.y;
    a->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, CLS_NAME, APP_NAME, WS_POPUP,
                              wr.left, wr.top, w, h,
                              NULL, NULL, hi, a);
    if (!a->hwnd) return 0;
    a->W = w;
    a->H = h;
    return 1;
}

/* ---------------- 消息处理 ---------------- */

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    App *a = (App *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (a) ui_paint(a, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        if (a) {
            a->W = LOWORD(lp);
            a->H = HIWORD(lp);
            if (a->memdc) { DeleteDC(a->memdc); a->memdc = NULL; }
            if (a->membmp) { DeleteObject(a->membmp); a->membmp = NULL; }
            a->memw = a->memh = 0;
            widget_apply_region(a);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_NCHITTEST:
        if (a) {
            POINT p;
            RECT r;
            p.x = GET_X_LPARAM(lp);
            p.y = GET_Y_LPARAM(lp);
            ScreenToClient(hwnd, &p);
            /* 收起状态下整条细条都算客户区：左右贴边时露出来的正是窗口最外侧那 5px，
               按边框判成 HTLEFT/HTRIGHT 的话鼠标移过去只产生 WM_NCMOUSEMOVE，
               收起的窗口就永远唤不出来（贴顶时露的是底部行区，所以没这个毛病） */
            if (a->cfg.peek) return HTCLIENT;
            if (p.x < sc(a, 4)) return HTLEFT;
            if (p.x >= a->W - sc(a, 4)) return HTRIGHT;
            if (p.y < a->header_h) {
                ui_hdr_btn_rect(a, 1, &r);
                if (PtInRect(&r, p)) return HTCLIENT;
                ui_hdr_btn_rect(a, 0, &r);
                if (PtInRect(&r, p)) return HTCLIENT;
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        return HTCLIENT;

    case WM_EXITSIZEMOVE:
        if (a) after_move(a);
        return 0;

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        TRACKMOUSEEVENT tme;
        if (!a) return 0;
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        tme.dwHoverTime = 0;
        TrackMouseEvent(&tme);
        if (a->cfg.peek && !a->animating) {
            /* 鼠标碰到收起的细条 → 滑出来。收起动画还没跑完时不要抢。
               再加一道保险：确认真光标此刻真的压在窗口上——窗口刚滑走时
               系统可能补发一条过期的 WM_MOUSEMOVE，不校验的话会立刻弹回来。 */
            POINT cp;
            RECT wr2;
            GetCursorPos(&cp);
            GetWindowRect(hwnd, &wr2);
            if (PtInRect(&wr2, cp)) {
                int rx, ry;
                a->cfg.peek = 0;
                revealed_pos(a, &rx, &ry);
                SetWindowPos(a->hwnd, NULL, rx, ry, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                widget_save_pos(a);
            }
        }
        input_mouse_move(a, x, y);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (a) {
            a->hover_row = -1;
            a->hover_btn = 0;
            InvalidateRect(hwnd, NULL, FALSE);
            /* 指针离开不再自动收起：只有把窗口拖到屏幕边上才会收起 */
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (a) { SetFocus(hwnd); input_lbutton_down(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); }
        return 0;
    case WM_LBUTTONUP:
        if (a) input_lbutton_up(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_LBUTTONDBLCLK:
        if (a) input_lbutton_dbl(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_RBUTTONUP:
        if (a) input_rbutton_up(a, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEWHEEL:
        if (a) {
            POINT p;
            p.x = GET_X_LPARAM(lp);
            p.y = GET_Y_LPARAM(lp);
            ScreenToClient(hwnd, &p);
            input_wheel(a, GET_WHEEL_DELTA_WPARAM(wp), p.x, p.y);
        }
        return 0;

    case WM_SETCURSOR:
        if (a && LOWORD(lp) == HTCLIENT) {
            POINT p;
            Hit h;
            GetCursorPos(&p);
            ScreenToClient(hwnd, &p);
            h = ui_hit(a, p.x, p.y);
            if (h.type == HIT_CHECK || h.type == HIT_DEL || h.type == HIT_BTN_ADD ||
                h.type == HIT_BTN_MENU || h.type == HIT_ADD) {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
            if (h.type == HIT_ROW) {
                SetCursor(LoadCursorW(NULL, IDC_ARROW));
                return TRUE;
            }
        }
        break;

    case WM_KEYDOWN:
        if (a) input_key(a, wp);
        return 0;
    case WM_CHAR:
        if (a) input_char(a, wp);
        return 0;
    case WM_SETFOCUS:
        if (a && a->editing >= 0) widget_set_caret(a);
        return 0;
    case WM_KILLFOCUS:
        if (a) { HideCaret(hwnd); if (a->editing >= 0) input_commit(a); }
        return 0;

    case WM_TIMER:
        if (!a) return 0;
        if (wp == T_ANIM) { do_anim(a); return 0; }
        if (wp == T_DAY) { InvalidateRect(hwnd, NULL, FALSE); return 0; }
        if (wp == T_SYNC) { sync_tick(a); return 0; }
        return 0;

    case WM_HOTKEY:
        if (a && wp == ID_HOTKEY) {
            if (IsWindowVisible(hwnd) && GetForegroundWindow() == hwnd) {
                widget_hide_to_tray(a);
            } else {
                widget_show(a, 1);
                SetForegroundWindow(hwnd);
                widget_set_caret(a);
            }
        }
        return 0;

    case WM_TRAY:
        if (a) {
            if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) {
                if (IsWindowVisible(hwnd)) widget_hide_to_tray(a);
                else { widget_show(a, 1); SetForegroundWindow(hwnd); }
            } else if (lp == WM_RBUTTONUP) {
                tray_menu(a);
            }
        }
        return 0;

    case WM_SHOWAPP:
        if (a) {
            widget_show(a, 1);
            SetForegroundWindow(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_CLOSE:
        if (a) widget_hide_to_tray(a);
        return 0;

    case WM_DPICHANGED:
        if (a) {
            a->dpi = HIWORD(wp);
            theme_init(a);
            widget_fit(a);
            widget_apply_region(a);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_DESTROY:
        detail_destroy();          /* 详情窗还开着的话，连带关掉，别留下悬空的模态循环 */
        courses_destroy();         /* 筛选课程窗口同理 */
        theme_dlg_destroy();       /* 主题窗口同理 */
        if (a) sync_cancel(a);     /* 同步子进程还没结束就杀掉，别留孤儿 powershell */
        if (a && !a->quit) {
            a->quit = 1;
            widget_tray_remove(a);
        }
        if (a && a->memdc) { DeleteDC(a->memdc); a->memdc = NULL; }
        if (a && a->membmp) { DeleteObject(a->membmp); a->membmp = NULL; }
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
