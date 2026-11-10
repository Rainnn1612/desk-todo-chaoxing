/* input.c - 鼠标 / 键盘交互、行内编辑、拖拽调序、右键菜单 */
#include "app.h"

#define MENU_ADD      1001
#define MENU_EDIT     1002
#define MENU_DEL      1003
#define MENU_TOP      1004
#define MENU_PEEK     1005
#define MENU_AUTO     1006
#define MENU_THEME    1007
#define MENU_SORT     1008
#define MENU_QUIT     1009
#define MENU_FONT_1   1010      /* 小 */
#define MENU_FONT_2   1011      /* 标准 */
#define MENU_FONT_3   1012      /* 大 */
#define MENU_FONT_4   1013      /* 特大 */
#define MENU_FONT_IN  1014      /* 更大 +10% */
#define MENU_FONT_OUT 1015      /* 更小 -10% */
#define MENU_DETAIL   1016      /* 备注与图片 */
#define MENU_SYNC     1017      /* 立即同步学习通 */
#define MENU_COURSES  1018      /* 筛选课程（旧课退休名单） */
#define MENU_THEME_DLG 1019     /* 主题…（自定义配色 + 玻璃） */

/* 字号档位（百分比） */
static const int FONT_STEPS[4] = { 85, 100, 115, 135 };

int input_is_printable(WPARAM ch)
{
    return (ch >= 32 && ch != 127);
}

/* ---------------- 编辑 ---------------- */

void input_start_edit(App *a, int view_index, int is_new)
{
    if (is_new) {
        a->editing = a->todos.n;      /* 新行画在列表末尾 */
        a->edit_new = 1;
        a->edit[0] = 0;
        a->editlen = 0;
        a->caret = 0;
    } else {
        int ti;
        if (view_index < 0 || view_index >= a->todos.n) return;
        a->editing = view_index;
        a->edit_new = 0;
        ti = a->todos.view[view_index];
        wcpy(a->edit, a->todos.v[ti].title, MAX_TITLE);
        a->caret = wlen(a->edit);     /* 光标停在标题末尾 */
        if (a->todos.v[ti].has_ddl) { /* 把 DDL 一并带进来，编辑标题就不会丢 DDL */
            WCHAR d[32];
            ddl_to_text(&a->todos.v[ti].ddl, d, 32);
            wcat(a->edit, L" ", MAX_TITLE);
            wcat(a->edit, d, MAX_TITLE);
        }
        a->editlen = wlen(a->edit);
    }
    a->scroll = 0;
    widget_fit(a);
    InvalidateRect(a->hwnd, NULL, FALSE);
    widget_set_caret(a);
}

void input_cancel(App *a)
{
    if (a->editing < 0) return;
    a->editing = -1;
    a->edit_new = 0;
    a->edit[0] = 0;
    a->editlen = 0;
    a->caret = 0;
    DestroyCaret();
    widget_fit(a);
    InvalidateRect(a->hwnd, NULL, FALSE);
}

void input_commit(App *a)
{
    if (a->editing < 0) return;
    if (a->edit_new) {
        if (a->editlen > 0) store_add(a, a->edit);
    } else if (a->editing < a->todos.n) {
        store_update(a, a->todos.view[a->editing], a->edit);
    }
    a->editing = -1;
    a->edit_new = 0;
    a->edit[0] = 0;
    a->editlen = 0;
    a->caret = 0;
    DestroyCaret();
    widget_fit(a);
    InvalidateRect(a->hwnd, NULL, FALSE);
}

/* ---------------- 鼠标 ---------------- */

void input_mouse_move(App *a, int x, int y)
{
    Hit h;
    int old_row = a->hover_row, old_btn = a->hover_btn;

    if (a->dragging) {
        int vp = ui_viewport_h(a);
        int yy = y - a->header_h + a->scroll;
        int to = (yy + a->row_h / 2) / a->row_h;
        if (to < 0) to = 0;
        if (to > ui_row_count(a)) to = ui_row_count(a);
        if (to != a->drag_to) { a->drag_to = to; InvalidateRect(a->hwnd, NULL, FALSE); }
        if (y < a->header_h + sc(a, 10) && a->scroll > 0) {
            a->scroll -= sc(a, 8);
            if (a->scroll < 0) a->scroll = 0;
            InvalidateRect(a->hwnd, NULL, FALSE);
        }
        if (y > a->header_h + vp - sc(a, 10) && ui_content_h(a) > vp) {
            int maxs = ui_content_h(a) - vp;
            a->scroll += sc(a, 8);
            if (a->scroll > maxs) a->scroll = maxs;
            InvalidateRect(a->hwnd, NULL, FALSE);
        }
        return;
    }

    if (a->pressed && a->press_row >= 0 &&
        (abs(x - a->press.x) > sc(a, 4) || abs(y - a->press.y) > sc(a, 4))) {
        if (!a->cfg.manual_order) {
            list_materialize_view(a);
            a->cfg.manual_order = 1;
            list_build_view(a);              /* 现在 view 是恒等映射 */
            cfg_save(a);
        }
        a->dragging = 1;
        a->drag_row = a->press_row;
        a->drag_to = a->press_row;
        InvalidateRect(a->hwnd, NULL, FALSE);
        return;
    }

    h = ui_hit(a, x, y);
    a->hover_btn = (h.type == HIT_BTN_ADD) ? 1 : (h.type == HIT_BTN_MENU ? 2 : 0);
    if (h.type == HIT_ROW || h.type == HIT_CHECK || h.type == HIT_DEL) a->hover_row = h.index;
    else if (h.type == HIT_ADD || h.type == HIT_BLANK) a->hover_row = -2;
    else a->hover_row = -1;

    if (old_row != a->hover_row || old_btn != a->hover_btn)
        InvalidateRect(a->hwnd, NULL, FALSE);
}

void input_lbutton_down(App *a, int x, int y)
{
    Hit h;

    if (a->editing >= 0) input_commit(a);

    h = ui_hit(a, x, y);
    a->press.x = x;
    a->press.y = y;
    a->pressed = 1;
    a->press_row = (h.type == HIT_ROW) ? h.index : -1;
    a->dragging = 0;

    if (h.type == HIT_BTN_ADD) {
        a->pressed = 0;
        input_start_edit(a, -1, 1);
        return;
    }
    if (h.type == HIT_BTN_MENU) {
        /* 标题栏那个 ☰ 也得能点开菜单，不然它只是个画上去的装饰 */
        a->pressed = 0;
        a->press_row = -1;
        input_context_menu(a, x, y);
        return;
    }
    if (h.type == HIT_ADD) {
        a->pressed = 0;
        input_start_edit(a, -1, 1);
        return;
    }
    if (h.type == HIT_CHECK && h.index >= 0 && h.index < a->todos.n) {
        Todo *t = &a->todos.v[a->todos.view[h.index]];
        t->done = !t->done;
        list_build_view(a);
        store_save(a);
        a->pressed = 0;
        InvalidateRect(a->hwnd, NULL, FALSE);
        return;
    }
    if (h.type == HIT_DEL && h.index >= 0 && h.index < a->todos.n) {
        store_remove(a, a->todos.view[h.index]);
        a->pressed = 0;
        a->hover_row = -1;
        widget_fit(a);
        InvalidateRect(a->hwnd, NULL, FALSE);
        return;
    }
    if (h.type == HIT_NOTE && h.index >= 0 && h.index < a->todos.n) {
        a->pressed = 0;
        a->press_row = -1;
        detail_open(a, a->todos.view[h.index]);
        return;
    }
}

void input_lbutton_up(App *a, int x, int y)
{
    (void)x; (void)y;
    if (a->dragging) {
        int from = a->drag_row, to = a->drag_to;
        a->dragging = 0;
        a->drag_row = -1;
        a->hover_row = -1;      /* 落点变了，等下一次 mousemove 重新判定悬停行 */
        list_reorder(a, from, to);
        store_save(a);
        a->scroll = 0;
        widget_fit(a);
        InvalidateRect(a->hwnd, NULL, FALSE);
    }
    a->pressed = 0;
    a->press_row = -1;
}

void input_lbutton_dbl(App *a, int x, int y)
{
    Hit h = ui_hit(a, x, y);
    if (h.type == HIT_ROW && h.index >= 0 && h.index < a->todos.n)
        input_start_edit(a, h.index, 0);
    else if (h.type == HIT_ADD || h.type == HIT_BLANK)
        input_start_edit(a, -1, 1);
}

void input_wheel(App *a, int delta, int x, int y)
{
    int vp = ui_viewport_h(a);
    int maxs = ui_content_h(a) - vp;
    int step;
    (void)x; (void)y;
    if (maxs <= 0) return;
    step = (abs(delta) / WHEEL_DELTA) * sc(a, 36);
    if (step == 0) step = sc(a, 36);
    a->scroll -= (delta > 0 ? step : -step);
    if (a->scroll < 0) a->scroll = 0;
    if (a->scroll > maxs) a->scroll = maxs;
    InvalidateRect(a->hwnd, NULL, FALSE);
}

/* ---------------- 键盘 ---------------- */

void input_char(App *a, WPARAM ch)
{
    if (a->editing < 0) {
        if (input_is_printable(ch)) {
            input_start_edit(a, -1, 1);
            if (a->editlen < MAX_TITLE - 1) {
                a->edit[a->editlen++] = (WCHAR)ch;
                a->edit[a->editlen] = 0;
                a->caret = a->editlen;
                widget_set_caret(a);
                InvalidateRect(a->hwnd, NULL, FALSE);
            }
        }
        return;
    }
    if (ch == 8 || ch == 13 || ch == 27 || ch == 9) return;   /* 交给 WM_KEYDOWN */
    if (a->editlen >= MAX_TITLE - 1) return;
    if (a->caret < 0) a->caret = 0;
    if (a->caret > a->editlen) a->caret = a->editlen;
    {
        int i;
        for (i = a->editlen; i > a->caret; i--) a->edit[i] = a->edit[i - 1];
        a->edit[a->caret] = (WCHAR)ch;
        a->editlen++;
        a->edit[a->editlen] = 0;
        a->caret++;
    }
    widget_set_caret(a);
    InvalidateRect(a->hwnd, NULL, FALSE);
}

void input_key(App *a, WPARAM key)
{
    int vp = ui_viewport_h(a);
    int maxs = ui_content_h(a) - vp;

    if (a->editing >= 0) {
        switch (key) {
        case VK_LEFT:  if (a->caret > 0) a->caret--; break;
        case VK_RIGHT: if (a->caret < a->editlen) a->caret++; break;
        case VK_HOME:  a->caret = 0; break;
        case VK_END:   a->caret = a->editlen; break;
        case VK_BACK:
            if (a->caret > 0) {
                int i;
                for (i = a->caret - 1; i < a->editlen - 1; i++) a->edit[i] = a->edit[i + 1];
                a->editlen--;
                a->edit[a->editlen] = 0;
                a->caret--;
            }
            break;
        case VK_DELETE:
            if (a->caret < a->editlen) {
                int i;
                for (i = a->caret; i < a->editlen - 1; i++) a->edit[i] = a->edit[i + 1];
                a->editlen--;
                a->edit[a->editlen] = 0;
            }
            break;
        case VK_RETURN: input_commit(a); return;
        case VK_ESCAPE: input_cancel(a); return;
        default: return;
        }
        widget_set_caret(a);
        InvalidateRect(a->hwnd, NULL, FALSE);
        return;
    }

    switch (key) {
    case VK_UP:
        a->scroll -= a->row_h;
        if (a->scroll < 0) a->scroll = 0;
        InvalidateRect(a->hwnd, NULL, FALSE);
        break;
    case VK_DOWN:
        if (maxs > 0) {
            a->scroll += a->row_h;
            if (a->scroll > maxs) a->scroll = maxs;
            InvalidateRect(a->hwnd, NULL, FALSE);
        }
        break;
    case VK_RETURN: input_start_edit(a, -1, 1); break;
    case VK_ESCAPE: widget_hide_to_tray(a); break;
    default: break;
    }
}

/* ---------------- 右键菜单 ---------------- */

/* 改字号：字体、界面骨架、窗口尺寸、圆角、贴边位置全部跟着重算 */
static void apply_font(App *a, int pct)
{
    if (pct < FONT_MIN) pct = FONT_MIN;
    if (pct > FONT_MAX) pct = FONT_MAX;
    if (pct == a->cfg.font) return;
    a->cfg.font = pct;
    theme_init(a);              /* 重建字体，内部会调 ui_metrics */
    widget_fit(a);              /* 行高/宽度变了，窗口尺寸跟着变 */
    widget_realign(a);          /* 贴边展开状态要重新对齐那条边 */
    widget_apply_region(a);
    cfg_save(a);
    InvalidateRect(a->hwnd, NULL, FALSE);
}

void input_context_menu(App *a, int x, int y)
{
    HMENU m = CreatePopupMenu();
    Hit h;
    RECT wr;
    POINT pt;
    UINT cmd;
    int can_edit;

    h = ui_hit(a, x, y);
    can_edit = (h.type == HIT_ROW && h.index >= 0 && h.index < a->todos.n);

    AppendMenuW(m, MF_STRING, MENU_ADD, L"新增待办");
    AppendMenuW(m, MF_STRING | (can_edit ? 0 : MF_GRAYED), MENU_EDIT, L"编辑");
    AppendMenuW(m, MF_STRING | (can_edit ? 0 : MF_GRAYED), MENU_DEL, L"删除");
    AppendMenuW(m, MF_STRING | (can_edit ? 0 : MF_GRAYED), MENU_DETAIL, L"备注与图片…");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (a->cfg.topmost ? MF_CHECKED : 0), MENU_TOP, L"总在最前");
    AppendMenuW(m, MF_STRING | (a->cfg.edge ? MF_CHECKED : 0), MENU_PEEK, L"贴边隐藏（拖到屏幕边松手）");
    AppendMenuW(m, MF_STRING | (a->cfg.autostart ? MF_CHECKED : 0), MENU_AUTO, L"开机自动启动");
    AppendMenuW(m, MF_STRING | (a->cfg.theme == 1 ? MF_CHECKED : 0), MENU_THEME, L"浅色主题");
    AppendMenuW(m, MF_STRING | (a->cfg.theme == 2 ? MF_CHECKED : 0), MENU_THEME_DLG,
                a->cfg.glass ? L"主题…（玻璃）" : L"主题…");
    AppendMenuW(m, MF_STRING | (a->cfg.manual_order ? 0 : MF_CHECKED), MENU_SORT, L"按 DDL 排序");
    {
        /* 字号子菜单：4 个档位 + 微调；档位不匹配时没有任何一项打勾 */
        HMENU fm = CreatePopupMenu();
        int i, cur = a->cfg.font;
        WCHAR lab[48];
        for (i = 0; i < 4; i++) {
            const WCHAR *nm = (i == 0) ? L"小  85%" : (i == 1) ? L"标准 100%"
                            : (i == 2) ? L"大  115%" : L"特大 135%";
            wcpy(lab, nm, 48);
            AppendMenuW(fm, MF_STRING | (cur == FONT_STEPS[i] ? MF_CHECKED : 0),
                        MENU_FONT_1 + i, lab);
        }
        AppendMenuW(fm, MF_SEPARATOR, 0, NULL);
        wcpy(lab, L"更大 +10%", 48);
        AppendMenuW(fm, MF_STRING | (cur >= FONT_MAX ? MF_GRAYED : 0), MENU_FONT_IN, lab);
        wcpy(lab, L"更小 -10%", 48);
        AppendMenuW(fm, MF_STRING | (cur <= FONT_MIN ? MF_GRAYED : 0), MENU_FONT_OUT, lab);
        AppendMenuW(m, MF_POPUP | MF_STRING, (UINT_PTR)fm, L"字体大小");
    }
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (a->syncing ? MF_GRAYED : 0), MENU_SYNC,
                a->syncing ? L"同步中…" : L"立即同步学习通");
    AppendMenuW(m, MF_STRING, MENU_COURSES, L"筛选课程");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, MENU_QUIT, L"退出");

    GetWindowRect(a->hwnd, &wr);
    pt.x = wr.left + x;
    pt.y = wr.top + y;
    SetForegroundWindow(a->hwnd);
    cmd = (UINT)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, a->hwnd, NULL);
    DestroyMenu(m);
    PostMessageW(a->hwnd, WM_NULL, 0, 0);

    switch (cmd) {
    case MENU_ADD:
        input_start_edit(a, -1, 1);
        break;
    case MENU_EDIT:
        if (can_edit) input_start_edit(a, h.index, 0);
        break;
    case MENU_DEL:
        if (can_edit) {
            store_remove(a, a->todos.view[h.index]);
            widget_fit(a);
            InvalidateRect(a->hwnd, NULL, FALSE);
        }
        break;
    case MENU_DETAIL:
        if (can_edit) detail_open(a, a->todos.view[h.index]);
        break;
    case MENU_TOP:
        a->cfg.topmost = !a->cfg.topmost;
        widget_set_topmost(a, a->cfg.topmost);
        cfg_save(a);
        break;
    case MENU_PEEK:
        /* 默认贴顶：开启后立刻收起一次，之后拖到任意屏幕边松手都会收起 */
        a->cfg.edge = a->cfg.edge ? 0 : 3;
        a->cfg.peek = 0;
        cfg_save(a);
        widget_check_edge(a);
        break;
    case MENU_AUTO:
        a->cfg.autostart = !a->cfg.autostart;
        autostart_set(a->cfg.autostart);
        cfg_save(a);
        break;
    case MENU_THEME:
        a->cfg.theme = (a->cfg.theme == 1) ? 0 : 1;
        cfg_save(a);
        InvalidateRect(a->hwnd, NULL, FALSE);
        break;
    case MENU_THEME_DLG:
        theme_dlg_open(a);
        break;
    case MENU_SORT:
        if (a->cfg.manual_order) {
            a->cfg.manual_order = 0;
            list_build_view(a);
            cfg_save(a);
            a->scroll = 0;
            InvalidateRect(a->hwnd, NULL, FALSE);
        }
        break;
    case MENU_SYNC:
        if (!a->syncing) sync_start(a);
        break;
    case MENU_COURSES:
        courses_open(a);
        break;
    case MENU_QUIT:
        widget_quit(a);
        break;
    case MENU_FONT_1:
    case MENU_FONT_2:
    case MENU_FONT_3:
    case MENU_FONT_4:
        apply_font(a, FONT_STEPS[cmd - MENU_FONT_1]);
        break;
    case MENU_FONT_IN:
        apply_font(a, a->cfg.font + 10);
        break;
    case MENU_FONT_OUT:
        apply_font(a, a->cfg.font - 10);
        break;
    default:
        break;
    }
}

void input_rbutton_up(App *a, int x, int y)
{
    input_context_menu(a, x, y);
}
