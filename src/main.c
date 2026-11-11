/* main.c - 入口：单实例、DPI、装配、消息循环 */
#include "app.h"

static void set_dpi_aware(void)
{
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (u) {
        typedef BOOL (WINAPI *PFN)(void *);
        PFN f = NULL;
        FARPROC p = GetProcAddress(u, "SetProcessDpiAwarenessContext");
        if (p) {
            memcpy(&f, &p, sizeof(f));
            /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4 */
            if (f((void *)(LONG_PTR)-4)) return;
        }
    }
    SetProcessDPIAware();
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmdline, int show)
{
    App   *a;
    HANDLE mutex;
    MSG    msg;
    int    w;
    int    was_peeked = 0;

    (void)hPrev;
    (void)cmdline;
    (void)show;

    mutex = CreateMutexW(NULL, FALSE, APP_MUTEX);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND wnd = FindWindowW(L"DeskTodoWnd", NULL);
        if (wnd) PostMessageW(wnd, WM_SHOWAPP, 0, 0);
        CloseHandle(mutex);
        return 0;
    }

    set_dpi_aware();

    a = (App *)calloc(1, sizeof(App));
    if (!a) return 1;
    a->hi = hInst;
    a->editing = -1;
    a->hover_row = -1;
    a->drag_row = -1;

    store_init(a);
    ensure_data_dir();          /* 全新电脑上这个目录还不存在，先建出来 */
    cfg_load(a);
    a->cfg.autostart = autostart_get();

    if (!widget_create(a, hInst)) {
        free(a);
        if (mutex) CloseHandle(mutex);
        return 1;
    }
    theme_init(a);
    store_load(a);
    trash_load(a);
    list_build_view(a);

    /* 上次贴了边：先对齐到那条边，再按上次的收起/展开状态复位 */
    if (a->cfg.edge) {
        RECT wa;
        was_peeked = a->cfg.peek;
        widget_work_area(a->hwnd, &wa);
        w = fz(a, a->cfg.width);
        if (a->cfg.edge == 2)      a->cfg.x = wa.right - w;
        else if (a->cfg.edge == 1) a->cfg.x = wa.left;
        else                       a->cfg.y = wa.top;
        a->cfg.peek = 0;
        SetWindowPos(a->hwnd, NULL, a->cfg.x, a->cfg.y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    widget_fit(a);
    /* 收起位要按「最终高度」算：上面 widget_fit 之前窗口还是空的，高度偏小，
       那时摆收起位会算出偏高的 y，等数据加载完窗口变高就会露出大半截。 */
    if (a->cfg.edge && was_peeked) {
        int tx, ty;
        a->cfg.peek = 1;
        widget_peek_pos(a, &tx, &ty);
        SetWindowPos(a->hwnd, NULL, tx, ty, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        a->cfg.x = tx;
        a->cfg.y = ty;
    }

    widget_fit(a);
    widget_apply_region(a);
    widget_set_topmost(a, a->cfg.topmost);
    widget_tray_add(a);

    RegisterHotKey(a->hwnd, ID_HOTKEY, MOD_CONTROL | MOD_ALT, 'T');
    SetTimer(a->hwnd, T_DAY, 60000, NULL);

    ShowWindow(a->hwnd, SW_SHOWNOACTIVATE);
    theme_apply_glass(a);          /* 上次若开着玻璃，启动就恢复 */
    InvalidateRect(a->hwnd, NULL, FALSE);

    /* 支持命令行直接同步：DeskTodo.exe --sync
       （方便用任务计划程序定时跑，也方便我这边的自动化测试） */
    {
        const WCHAR *cl = GetCommandLineW();
        if (cl && wcsstr(cl, L"--sync")) sync_start(a);
        else if (cl && wcsstr(cl, L"--courses")) courses_open(a);
        else if (cl && wcsstr(cl, L"--theme")) theme_dlg_open(a);
        else if (cl && wcsstr(cl, L"--login")) login_open(a);
        else if (cl && wcsstr(cl, L"--trash")) trash_open(a);
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnregisterHotKey(a->hwnd, ID_HOTKEY);
    cfg_save(a);
    store_save(a);
    widget_tray_remove(a);
    theme_free(a);
    store_free(a);
    if (mutex) CloseHandle(mutex);
    free(a);
    return 0;
}
