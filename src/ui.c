/* ui.c - 布局度量、自绘（GDI 双缓冲）、命中测试 */
#include "app.h"

#define MAXI(x, y) ((x) > (y) ? (x) : (y))

/* ---------------- 度量 ---------------- */

void ui_metrics(App *a)
{
    a->header_h = fz(a, 30);
    a->row_h    = fz(a, 34);
    a->add_h    = fz(a, 32);
    a->pad      = fz(a, 10);
    a->chk      = fz(a, 15);
    a->sb_w     = fz(a, 4);
}

int ui_row_count(App *a) { return a->todos.n + (a->edit_new ? 1 : 0); }

int ui_content_h(App *a) { return ui_row_count(a) * a->row_h; }

int ui_viewport_h(App *a)
{
    int h = ui_content_h(a);
    int maxv = MAX_VIS_ROWS * a->row_h;
    if (h > maxv) h = maxv;
    if (h < 0) h = 0;
    return h;
}

int ui_needed_height(App *a)
{
    return a->header_h + ui_viewport_h(a) + a->add_h + fz(a, 2);
}

static void row_rect(App *a, int i, RECT *r)
{
    r->left = 0;
    r->right = a->W;
    r->top = a->header_h + i * a->row_h - a->scroll;
    r->bottom = r->top + a->row_h;
}

static void chk_rect(App *a, int i, RECT *r)
{
    int s = a->chk;
    r->left = fz(a, 12);
    r->right = r->left + s;
    r->top = a->header_h + i * a->row_h - a->scroll + (a->row_h - s) / 2;
    r->bottom = r->top + s;
}

static void del_rect(App *a, int i, RECT *r)
{
    int s = a->chk + fz(a, 4);
    r->right = a->W - fz(a, 8);
    r->left = r->right - s;
    r->top = a->header_h + i * a->row_h - a->scroll + (a->row_h - s) / 2;
    r->bottom = r->top + s;
}

/* 行内那个「图」/「文」小标记：有说明或图片才出现，点它打开详情窗 */
static int row_badge(App *a, int i, RECT *r)
{
    RECT rr;
    Todo *t;
    int w, right;

    if (i < 0 || i >= a->todos.n) return 0;
    t = &a->todos.v[a->todos.view[i]];
    if (!((t->note && t->note[0]) || (t->img && t->img[0]))) return 0;

    row_rect(a, i, &rr);
    right = rr.right - fz(a, 10);
    if (a->hover_row == i && !a->dragging) right -= fz(a, 18);
    w = fz(a, 15);
    r->right = right;
    r->left = right - w;
    r->top = rr.top + (a->row_h - w) / 2;
    r->bottom = r->top + w;
    return 1;
}

void ui_hdr_btn_rect(App *a, int which, RECT *r)
{
    int s = a->header_h - fz(a, 6);
    r->right = a->W - fz(a, 6) - (which ? s + fz(a, 2) : 0);
    r->left = r->right - s;
    r->top = fz(a, 3);
    r->bottom = r->top + s;
}

static void edit_rect(App *a, int i, RECT *r)
{
    row_rect(a, i, r);
    r->left += fz(a, 8);
    r->right -= fz(a, 8);
    r->top += fz(a, 3);
    r->bottom -= fz(a, 3);
}

POINT ui_caret_pos(App *a)
{
    POINT p;
    RECT er, tr;
    SIZE sz;
    WCHAR tmp[MAX_TITLE];
    int n = a->caret;
    p.x = 0; p.y = 0;
    if (a->editing < 0) return p;
    edit_rect(a, a->editing, &er);
    tr = er;
    tr.left += fz(a, 6);
    if (n < 0) n = 0;
    if (n > a->editlen) n = a->editlen;
    memcpy(tmp, a->edit, (size_t)n * sizeof(WCHAR));
    tmp[n] = 0;
    {
        HDC dc = GetDC(a->hwnd);
        HGDIOBJ o = SelectObject(dc, a->f_title);
        GetTextExtentPoint32W(dc, tmp, n, &sz);
        SelectObject(dc, o);
        ReleaseDC(a->hwnd, dc);
    }
    p.x = tr.left + sz.cx;
    p.y = er.top + fz(a, 5);
    return p;
}

/* ---------------- 命中测试 ---------------- */

Hit ui_hit(App *a, int x, int y)
{
    Hit h;
    RECT r;
    POINT pt;
    int vp = ui_viewport_h(a);
    h.type = HIT_NONE; h.index = -1; h.sub = 0;
    pt.x = x; pt.y = y;

    if (y < a->header_h) {
        ui_hdr_btn_rect(a, 1, &r);
        if (PtInRect(&r, pt)) { h.type = HIT_BTN_ADD; return h; }
        ui_hdr_btn_rect(a, 0, &r);
        if (PtInRect(&r, pt)) { h.type = HIT_BTN_MENU; return h; }
        h.type = HIT_HEADER;
        return h;
    }
    if (y < a->header_h + vp) {
        int i = (y - a->header_h + a->scroll) / a->row_h;
        if (i >= 0 && i < ui_row_count(a)) {
            h.index = i;
            chk_rect(a, i, &r);
            if (PtInRect(&r, pt)) { h.type = HIT_CHECK; return h; }
            if (i < a->todos.n && i != a->editing) {
                del_rect(a, i, &r);
                if (a->hover_row == i && PtInRect(&r, pt)) { h.type = HIT_DEL; return h; }
                if (row_badge(a, i, &r) && PtInRect(&r, pt)) { h.type = HIT_NOTE; return h; }
            }
            h.type = HIT_ROW;
            return h;
        }
        return h;
    }
    if (ui_content_h(a) > vp && x >= a->W - fz(a, 8)) { h.type = HIT_SCROLL; return h; }
    h.type = HIT_ADD;
    return h;
}

/* ---------------- 绘制辅助 ---------------- */

static void fill(HDC dc, const RECT *r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, r, b);
    DeleteObject(b);
}

static void frame(HDC dc, const RECT *r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FrameRect(dc, r, b);
    DeleteObject(b);
}

static void line(HDC dc, int x1, int y1, int x2, int y2, COLORREF c, int w)
{
    HPEN p = CreatePen(PS_SOLID, w, c);
    HGDIOBJ o = SelectObject(dc, p);
    MoveToEx(dc, x1, y1, NULL);
    LineTo(dc, x2, y2);
    SelectObject(dc, o);
    DeleteObject(p);
}

static void text(HDC dc, HFONT f, const WCHAR *s, RECT *r, COLORREF c, UINT fmt)
{
    HGDIOBJ o = SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, -1, r, fmt);
    SelectObject(dc, o);
}

static int text_w(HDC dc, HFONT f, const WCHAR *s)
{
    SIZE sz;
    HGDIOBJ o = SelectObject(dc, f);
    GetTextExtentPoint32W(dc, s, (int)wlen(s), &sz);
    SelectObject(dc, o);
    return sz.cx;
}

static COLORREF level_color(App *a, int lv)
{
    switch (lv) {
    case 4: return theme_color(a, C_OVERDUE);
    case 3: return theme_color(a, C_TODAY);
    case 2: return theme_color(a, C_SOON);
    case 1: return theme_color(a, C_NORMAL);
    default: return theme_color(a, C_NORMAL);
    }
}

static void draw_check(HDC dc, App *a, const RECT *r, int done)
{
    int w = r->right - r->left, h = r->bottom - r->top;
    int pw = MAXI(1, fz(a, 2));
    if (done) fill(dc, r, theme_color(a, C_ACCENT));
    else frame(dc, r, theme_color(a, C_BOX));
    if (done) {
        line(dc, r->left + w * 26 / 100, r->top + h * 52 / 100,
                 r->left + w * 43 / 100, r->top + h * 70 / 100, RGB(255, 255, 255), pw);
        line(dc, r->left + w * 43 / 100, r->top + h * 70 / 100,
                 r->left + w * 75 / 100, r->top + h * 30 / 100, RGB(255, 255, 255), pw);
    }
}

/* ---------------- 主绘制 ---------------- */

static void ensure_mem(App *a, HDC ref)
{
    if (a->memdc && a->memw == a->W && a->memh == a->H) return;
    if (a->memdc)  { DeleteDC(a->memdc); a->memdc = NULL; }
    if (a->membmp) { DeleteObject(a->membmp); a->membmp = NULL; }
    a->memdc = CreateCompatibleDC(ref);
    a->membmp = CreateCompatibleBitmap(ref, MAXI(1, a->W), MAXI(1, a->H));
    SelectObject(a->memdc, a->membmp);
    a->memw = a->W;
    a->memh = a->H;
}

static void draw_row(HDC dc, App *a, int i)
{
    RECT rr, cr, dr, tr;
    int todoidx = (i < a->todos.n) ? a->todos.view[i] : -1;
    Todo *t = (todoidx >= 0) ? &a->todos.v[todoidx] : NULL;
    int lv = t ? ddl_level(t) : 0;
    int done = t ? t->done : 0;
    WCHAR dt[48];
    int dtw = 0;

    row_rect(a, i, &rr);

    if (a->editing == i) {
        RECT er;
        int tx;
        edit_rect(a, i, &er);
        fill(dc, &er, theme_color(a, C_EDITBG));
        frame(dc, &er, theme_color(a, C_ACCENT));
        tx = er.left + fz(a, 6);
        tr = er;
        tr.left = tx;
        tr.right -= fz(a, 6);
        if (a->editlen == 0) {
            text(dc, a->f_title, L"新待办…  例: 交房租 明天 18:00", &tr,
                 theme_color(a, C_MUTED), DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        } else {
            text(dc, a->f_title, a->edit, &tr, theme_color(a, C_TEXT),
                 DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        }
        return;
    }

    if (a->hover_row == i && !a->dragging) fill(dc, &rr, theme_color(a, C_HOVER));

    chk_rect(a, i, &cr);
    draw_check(dc, a, &cr, done);

    if (!t) return;

    ddl_text(t, dt, 48);
    if (dt[0]) dtw = text_w(dc, a->f_small, dt);

    /* 从右侧往里排：×（悬停时）→ 图/文标记 → DDL → 标题 → 说明后缀 */
    {
        int right = rr.right - fz(a, 10);
        int hover = (a->hover_row == i && !a->dragging);
        RECT bd;

        if (hover) right -= fz(a, 18);
        if (row_badge(a, i, &bd)) {
            text(dc, a->f_small,
                 (t->img && t->img[0]) ? L"图" : L"文", &bd,
                 theme_color(a, C_ACCENT),
                 DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
            right = bd.left - fz(a, 4);
        }

        tr = rr;
        tr.left = cr.right + fz(a, 9);
        tr.right = (dt[0] && dtw < right - tr.left) ? right - dtw - fz(a, 8) : right;
        text(dc, done ? a->f_strike : a->f_title, t->title, &tr,
             done ? theme_color(a, C_DONE) : theme_color(a, C_TEXT),
             DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);

        if (dt[0] && dtw < right - tr.left) {
            RECT er2;
            er2 = rr;
            er2.right = right;
            er2.left = right - dtw;
            text(dc, a->f_small, dt, &er2, level_color(a, lv),
                 DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
        }

        /* 说明文字：接着标题后面用浅色小字排一行，长了自己省略 */
        if (t->note && t->note[0]) {
            WCHAR suf[256];
            int tw, sx;
            note_head(t->note, suf, 200);
            tw = text_w(dc, done ? a->f_strike : a->f_title, t->title);
            sx = tr.left + tw + fz(a, 10);
            if (suf[0] && sx < tr.right - fz(a, 30)) {
                RECT sr = tr;
                sr.left = sx;
                text(dc, a->f_small, suf, &sr, theme_color(a, C_MUTED),
                     DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS |
                     DT_NOPREFIX);
            }
        }
    }

    if (a->hover_row == i && !done) {
        del_rect(a, i, &dr);
        text(dc, a->f_icon, L"\u00d7", &dr, theme_color(a, C_MUTED),
             DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }

    if (a->dragging && a->drag_to == i) {
        line(dc, rr.left + fz(a, 6), rr.top, rr.right - fz(a, 6), rr.top,
             theme_color(a, C_ACCENT), MAXI(2, fz(a, 2)));
    }
    if (lv == 4 && !done) {
        line(dc, rr.left, rr.top, rr.left + fz(a, 3), rr.top,
             theme_color(a, C_OVERDUE), MAXI(2, fz(a, 2)));
        line(dc, rr.left, rr.top, rr.left, rr.bottom,
             theme_color(a, C_OVERDUE), MAXI(2, fz(a, 2)));
    }
}

void ui_paint(App *a, HDC target)
{
    HDC dc;
    RECT r, vp;
    int i, vph;
    WCHAR cnt[160];

    if (a->W <= 0 || a->H <= 0) return;
    ensure_mem(a, target);
    dc = a->memdc;

    r.left = 0; r.top = 0; r.right = a->W; r.bottom = a->H;
    fill(dc, &r, theme_color(a, C_BG));

    /* 标题栏 */
    r.bottom = a->header_h;
    fill(dc, &r, theme_color(a, C_HEAD));
    line(dc, 0, a->header_h - 1, a->W, a->header_h - 1, theme_color(a, C_BORDER), 1);

    {
        RECT tr;
        int und = 0, j;
        for (j = 0; j < a->todos.n; j++) if (!a->todos.v[j].done) und++;
        tr.left = fz(a, 12);
        tr.right = a->W - fz(a, 70);
        tr.top = 0;
        tr.bottom = a->header_h;
        wcpy(cnt, APP_TITLE, 160);
        if (a->sync_msg[0]) {
            wcat(cnt, L"  ", 160);
            wcat(cnt, a->sync_msg, 160);
        } else {
            wcat(cnt, L"  ", 160);
            wcat_int(cnt, und, 160);
            wcat(cnt, L"/", 160);
            wcat_int(cnt, a->todos.n, 160);
        }
        text(dc, a->f_title, cnt, &tr,
             theme_color(a, a->sync_msg[0] ? C_ACCENT : C_TEXT),
             DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    }

    {
        RECT br;
        ui_hdr_btn_rect(a, 1, &br);
        if (a->hover_btn == 1) fill(dc, &br, theme_color(a, C_HOVER));
        text(dc, a->f_icon, L"+", &br, theme_color(a, C_ACCENT),
             DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        ui_hdr_btn_rect(a, 0, &br);
        if (a->hover_btn == 2) fill(dc, &br, theme_color(a, C_HOVER));
        text(dc, a->f_icon, L"\u2261", &br, theme_color(a, C_MUTED),
             DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }

    vph = ui_viewport_h(a);
    vp.left = 0; vp.top = a->header_h; vp.right = a->W; vp.bottom = a->header_h + vph;

    {
        int saved = SaveDC(dc);
        IntersectClipRect(dc, vp.left, vp.top, vp.right, vp.bottom);
        for (i = 0; i < ui_row_count(a); i++) {
            RECT rr;
            row_rect(a, i, &rr);
            if (rr.bottom <= a->header_h || rr.top >= a->header_h + vph) continue;
            draw_row(dc, a, i);
        }
        if (a->dragging && a->drag_to >= ui_row_count(a)) {
            int yy = a->header_h + ui_row_count(a) * a->row_h - a->scroll;
            if (yy <= a->header_h + vph)
                line(dc, fz(a, 6), yy, a->W - fz(a, 6), yy, theme_color(a, C_ACCENT), MAXI(2, fz(a, 2)));
        }
        RestoreDC(dc, saved);
    }

    /* 添加行 */
    {
        RECT ar;
        ar.left = 0;
        ar.right = a->W;
        ar.top = a->header_h + vph;
        ar.bottom = a->H;
        if (a->hover_row == -2) fill(dc, &ar, theme_color(a, C_HOVER));
        ar.left = fz(a, 12);
        text(dc, a->f_small, L"+ 添加待办", &ar, theme_color(a, C_MUTED),
             DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }

    /* 滚动条 */
    if (ui_content_h(a) > vph && vph > 0) {
        int total = ui_content_h(a);
        int barh = MAXI(16, vph * vph / total);
        int maxscroll = total - vph;
        int bary = a->header_h + (maxscroll > 0 ? (vph - barh) * a->scroll / maxscroll : 0);
        RECT sr;
        sr.left = a->W - fz(a, 4);
        sr.right = a->W - fz(a, 1);
        sr.top = bary;
        sr.bottom = bary + barh;
        fill(dc, &sr, theme_color(a, C_BOX));
    }

    /* 边框（圆角由窗口区域裁掉） */
    r.left = 0; r.top = 0; r.right = a->W; r.bottom = a->H;
    frame(dc, &r, theme_color(a, C_BORDER));

    BitBlt(target, 0, 0, a->W, a->H, dc, 0, 0, SRCCOPY);
}
