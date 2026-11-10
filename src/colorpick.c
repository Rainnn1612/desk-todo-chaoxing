/* colorpick.c - 自己画一个 Hsv 取色器（预设色块 + 明度/饱和度方块 + 色相条 +
 * Hex 显示 + R/G/B/A 四条滑块）。返回 COLORREF 与透明度。
 *
 * 为什么不用系统的 ChooseColorW：它长得跟随手截图那种取色器差得远，而且没有
 * 透明度通道。这里整窗自绘，鼠标拖动分四种拖拽对象，键盘只认 Esc / 回车。
 */
#include "app.h"

#define CLS_PICK L"DeskTodoPick"

#define SW_W   34          /* 预设色块 */
#define SW_H   24
#define SW_GAP 6
#define SQ_W   240         /* 饱和度/明度方块 */
#define SQ_H   200
#define HUE_W  18

#define K_BG   RGB(0xF2, 0xEF, 0xEA)
#define K_CARD RGB(0xFF, 0xFF, 0xFF)
#define K_TXT  RGB(0x3C, 0x38, 0x34)
#define K_MUT  RGB(0x8C, 0x86, 0x7E)
#define K_BORD RGB(0xC9, 0xC3, 0xBA)

typedef struct {
    HWND   hwnd;
    int    dpi, fs;
    HFONT  font, fontb;
    HBRUSH br_bg;
    int    h, s, v, a;          /* h 0..359, s/v/a 0..255 */
    HBITMAP sq, hue;            /* 缓存的两张渐变位图 */
    int    sq_h;                /* 生成 sq 时用的色相 */
    int    drag;                /* 0 无 1 方块 2 色相 3..6 RGBA 滑块 */
} Pick;

static Pick P;
static int g_done, g_ok;

static int pz(int val) { return MulDiv(MulDiv(val, P.dpi, 96), P.fs, 100); }

static HFONT pk_font(int qpt, int bold)
{
    int q = MulDiv(qpt * 4, P.fs, 100);
    if (q < 20) q = 20;
    return CreateFontW(-MulDiv(q, P.dpi, 4 * 72), 0, 0, 0,
                       bold ? FW_SEMIBOLD : FW_NORMAL, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Microsoft YaHei UI");
}

static void hsv2rgb(int h, int s, int v, int *r, int *g, int *b)
{
    int i, f, p, q, t, vv = v, ss = s;
    if (h < 0) h = 0;
    if (h > 359) h = 359;
    i = h / 60;
    f = h % 60;
    p = vv * (255 - ss) / 255;
    q = vv * (255 - (ss * f) / 60) / 255;
    t = vv * (255 - (ss * (60 - f)) / 60) / 255;
    switch (i) {
    case 0:  *r = vv; *g = t;  *b = p;  break;
    case 1:  *r = q;  *g = vv; *b = p;  break;
    case 2:  *r = p;  *g = vv; *b = t;  break;
    case 3:  *r = p;  *g = q;  *b = vv; break;
    case 4:  *r = t;  *g = p;  *b = vv; break;
    default: *r = vv; *g = p;  *b = q;  break;
    }
}

static void rgb2hsv(int r, int g, int b, int *h, int *s, int *v)
{
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    int d = mx - mn, hh = 0;
    if (d) {
        if (mx == r)      hh = 60 * (g - b) / d;
        else if (mx == g) hh = 120 + 60 * (b - r) / d;
        else              hh = 240 + 60 * (r - g) / d;
    }
    if (hh < 0) hh += 360;
    *h = hh;
    *s = mx ? d * 255 / mx : 0;
    *v = mx;
}

static void cur_rgb(int *r, int *g, int *b) { hsv2rgb(P.h, P.s, P.v, r, g, b); }

/* ---------------- 位图 ---------------- */

static void free_bm(void)
{
    if (P.sq)  { DeleteObject(P.sq);  P.sq = NULL; }
    if (P.hue) { DeleteObject(P.hue); P.hue = NULL; }
}

static HBITMAP make_sq(int w, int hgt)
{
    BITMAPINFO bi;
    HDC dc = GetDC(NULL);
    void *bits = NULL;
    HBITMAP bm;
    int x, y, r, g, b, *px;

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -hgt;          /* 负 = 自上而下 */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!bm || !bits) return NULL;
    px = (int *)bits;
    for (y = 0; y < hgt; y++) {
        int vv = (hgt > 1) ? (255 - y * 255 / (hgt - 1)) : 255;
        for (x = 0; x < w; x++) {
            int ss = (w > 1) ? (x * 255 / (w - 1)) : 255;
            hsv2rgb(P.h, ss, vv, &r, &g, &b);
            px[y * w + x] = (r << 16) | (g << 8) | b;   /* DIB 是 BGRA，小端写 int 正好 */
        }
    }
    return bm;
}

static HBITMAP make_hue(int w, int hgt)
{
    BITMAPINFO bi;
    HDC dc = GetDC(NULL);
    void *bits = NULL;
    HBITMAP bm;
    int x, y, r, g, b, *px;

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -hgt;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!bm || !bits) return NULL;
    px = (int *)bits;
    for (y = 0; y < hgt; y++) {
        int hh = (hgt > 1) ? (y * 359 / (hgt - 1)) : 0;
        hsv2rgb(hh, 255, 255, &r, &g, &b);
        for (x = 0; x < w; x++)
            px[y * w + x] = (r << 16) | (g << 8) | b;
    }
    return bm;
}

/* ---------------- 布局 ---------------- */

typedef struct { int x, y, w, h; } RC;

static void rect_sq(RC *r)
{
    r->x = pz(12); r->y = pz(44); r->w = pz(SQ_W); r->h = pz(SQ_H);
}
static void rect_hue(RC *r)
{
    r->x = pz(12 + SQ_W + 12); r->y = pz(44); r->w = pz(HUE_W); r->h = pz(SQ_H);
}
static void rect_sw(int i, RC *r)
{
    r->x = pz(12 + i * (SW_W + SW_GAP)); r->y = pz(10);
    r->w = pz(SW_W); r->h = pz(SW_H);
}
static void rect_slider(int i, RC *r)
{
    r->x = pz(44); r->y = pz(292 + i * 34); r->w = pz(240); r->h = pz(10);
}
static void rect_hex(RC *r)  { r->x = pz(12); r->y = pz(254); r->w = pz(150); r->h = pz(30); }
static void rect_prev(RC *r) { r->x = pz(172); r->y = pz(254); r->w = pz(80); r->h = pz(30); }
static void rect_btn(int ok, RC *r)
{
    r->x = pz(ok ? 200 : 300); r->y = pz(430);
    r->w = pz(88); r->h = pz(32);
}

static int in_rc(RC *r, int x, int y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* ---------------- 绘制 ---------------- */

static void draw_hex(HDC dc, RC *r)
{
    char buf[16];
    WCHAR w[16];
    int r8, g8, b8, i;
    HBRUSH b = CreateSolidBrush(K_CARD);
    HPEN p = CreatePen(PS_SOLID, 1, K_BORD);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    Rectangle(dc, r->x, r->y, r->x + r->w, r->y + r->h);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b); DeleteObject(p);

    cur_rgb(&r8, &g8, &b8);
    {
        static const char *HX = "0123456789ABCDEF";
        buf[0] = '#';
        for (i = 0; i < 6; i++) {
            int v6;
            if (i < 2)      v6 = (i == 0) ? (r8 >> 4) : (r8 & 15);
            else if (i < 4) v6 = (i == 2) ? (g8 >> 4) : (g8 & 15);
            else            v6 = (i == 4) ? (b8 >> 4) : (b8 & 15);
            buf[i + 1] = HX[v6];
        }
        buf[7] = 0;
    }
    for (i = 0; i < 8; i++) w[i] = (WCHAR)buf[i];
    w[8] = 0;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, K_TXT);
    {
        HGDIOBJ of = SelectObject(dc, P.fontb);
        RECT tr = { r->x + pz(10), r->y, r->x + r->w, r->y + r->h };
        DrawTextW(dc, w, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, of);
    }
}

static void draw_slider(HDC dc, int i)
{
    RC r, vr;
    static const WCHAR *NM[4] = { L"R", L"G", L"B", L"A" };
    int x, v, r8, g8, b8, k;
    char tmp[8];
    WCHAR wt[8];

    rect_slider(i, &r);
    vr.x = pz(292); vr.y = pz(292 + i * 34 - 6); vr.w = pz(70); vr.h = pz(22);

    cur_rgb(&r8, &g8, &b8);
    /* 轨道：0..255 渐变；A 轨道是从透明到当前色 */
    for (x = 0; x < r.w; x++) {
        int t = r.w > 1 ? x * 255 / (r.w - 1) : 255;
        int cr, cg, cb;
        if (i == 0)      { cr = t;      cg = g8;     cb = b8; }
        else if (i == 1) { cr = r8;     cg = t;      cb = b8; }
        else if (i == 2) { cr = r8;     cg = g8;     cb = t; }
        else             { cr = 238;    cg = 238;    cb = 238; }
        k = (i == 3) ? t : 255;
        if (i == 3) {
            /* 透明度轨道画成棋盘格的近似：直接按 alpha 把颜色往白底上压 */
            cr = (r8 * t + 238 * (255 - t)) / 255;
            cg = (g8 * t + 238 * (255 - t)) / 255;
            cb = (b8 * t + 238 * (255 - t)) / 255;
            k = 255;
        }
        (void)k;
        {
            HBRUSH b = CreateSolidBrush(RGB(cr, cg, cb));
            RECT c = { r.x + x, r.y, r.x + x + 1, r.y + r.h };
            FillRect(dc, &c, b);
            DeleteObject(b);
        }
    }
    /* 轨道边框 */
    {
        HPEN p = CreatePen(PS_SOLID, 1, K_BORD);
        HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, r.x - 1, r.y - 1, r.x + r.w + 1, r.y + r.h + 1);
        SelectObject(dc, op); SelectObject(dc, ob);
        DeleteObject(p);
    }
    v = (i == 0) ? r8 : (i == 1) ? g8 : (i == 2) ? b8 : P.a;
    /* 滑块 */
    {
        int tx = r.x + (r.w > 1 ? v * (r.w - 1) / 255 : 0);
        HBRUSH wb = CreateSolidBrush(RGB(255, 255, 255));
        HPEN   wp = CreatePen(PS_SOLID, 2, RGB(0x44, 0x40, 0x3C));
        HGDIOBJ ob = SelectObject(dc, wb), op = SelectObject(dc, wp);
        Ellipse(dc, tx - pz(7), r.y - pz(4), tx + pz(7), r.y + r.h + pz(4));
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(wb); DeleteObject(wp);
    }
    /* 名称 + 数值 */
    SetTextColor(dc, K_MUT);
    {
        HGDIOBJ of = SelectObject(dc, P.font);
        RECT nr = { pz(12), pz(292 + i * 34 - 8), pz(40), pz(292 + i * 34 + 18) };
        DrawTextW(dc, NM[i], -1, &nr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, of);
    }
    for (k = 0; k < 4; k++) { tmp[k] = 0; }
    if (v >= 100) { tmp[0] = (char)('0' + v / 100); tmp[1] = (char)('0' + (v / 10) % 10); tmp[2] = (char)('0' + v % 10); }
    else if (v >= 10) { tmp[0] = (char)('0' + v / 10); tmp[1] = (char)('0' + v % 10); }
    else { tmp[0] = (char)('0' + v); }
    for (k = 0; tmp[k]; k++) wt[k] = (WCHAR)tmp[k];
    wt[k] = 0;
    {
        HBRUSH b = CreateSolidBrush(K_CARD);
        HPEN p = CreatePen(PS_SOLID, 1, K_BORD);
        HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
        Rectangle(dc, vr.x, vr.y, vr.x + vr.w, vr.y + vr.h);
        SelectObject(dc, ob); SelectObject(dc, op);
        DeleteObject(b); DeleteObject(p);
    }
    SetTextColor(dc, K_TXT);
    {
        HGDIOBJ of = SelectObject(dc, P.font);
        RECT tr = { vr.x, vr.y, vr.x + vr.w, vr.y + vr.h };
        DrawTextW(dc, wt, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, of);
    }
}

static void draw_btn(HDC dc, int ok, int hot)
{
    RC r;
    WCHAR *t = ok ? L"确定" : L"取消";
    HBRUSH b = CreateSolidBrush(ok ? (hot ? RGB(0x4C, 0x8D, 0xFF) : RGB(0x3E, 0x74, 0xD0))
                                  : (hot ? RGB(0xE3, 0xDE, 0xD6) : RGB(0xEA, 0xE5, 0xDD)));
    HPEN p = CreatePen(PS_SOLID, 1, K_BORD);
    HGDIOBJ ob, op;
    rect_btn(ok, &r);
    ob = SelectObject(dc, b);
    op = SelectObject(dc, p);
    Rectangle(dc, r.x, r.y, r.x + r.w, r.y + r.h);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ok ? RGB(255, 255, 255) : K_TXT);
    {
        HGDIOBJ of = SelectObject(dc, P.fontb);
        RECT tr = { r.x, r.y, r.x + r.w, r.y + r.h };
        DrawTextW(dc, t, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, of);
    }
}

static void on_paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    HDC mem;
    HBITMAP mb;
    RECT cr;
    RC r;
    int i, r8, g8, b8;
    static const COLORREF SW[8] = {
        RGB(255,255,255), RGB(255,255,0), RGB(0,0,255), RGB(255,140,0),
        RGB(0,200,0), RGB(255,0,0), RGB(1,1,1), RGB(128,128,128)
    };

    GetClientRect(hwnd, &cr);
    mem = CreateCompatibleDC(dc);
    mb = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    SelectObject(mem, mb);

    /* 背景 */
    {
        HBRUSH b = CreateSolidBrush(K_BG);
        FillRect(mem, &cr, b);
        DeleteObject(b);
    }

    /* 预设色块 */
    for (i = 0; i < 8; i++) {
        HBRUSH b;
        rect_sw(i, &r);
        b = CreateSolidBrush(SW[i]);
        {
            HPEN p = CreatePen(PS_SOLID, 1, K_BORD);
            HGDIOBJ ob = SelectObject(mem, b), op = SelectObject(mem, p);
            Rectangle(mem, r.x, r.y, r.x + r.w, r.y + r.h);
            SelectObject(mem, ob);
            SelectObject(mem, op);
            DeleteObject(p);
        }
        DeleteObject(b);
    }

    /* 饱和度/明度方块 */
    if (!P.sq || P.sq_h != P.h) {
        if (P.sq) DeleteObject(P.sq);
        P.sq = make_sq(pz(SQ_W), pz(SQ_H));
        P.sq_h = P.h;
    }
    rect_sq(&r);
    if (P.sq) {
        HDC dc2 = CreateCompatibleDC(mem);
        HGDIOBJ ob = SelectObject(dc2, P.sq);
        BitBlt(mem, r.x, r.y, r.w, r.h, dc2, 0, 0, SRCCOPY);
        SelectObject(dc2, ob);
        DeleteDC(dc2);
    }
    /* 光标圈 */
    {
        int cx = r.x + P.s * (r.w - 1) / 255;
        int cy = r.y + (255 - P.v) * (r.h - 1) / 255;
        HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
        HPEN wp = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        HPEN bp = CreatePen(PS_SOLID, 1, RGB(60, 56, 52));
        HGDIOBJ ob = SelectObject(mem, nb), op = SelectObject(mem, bp);
        Ellipse(mem, cx - pz(6), cy - pz(6), cx + pz(6), cy + pz(6));
        SelectObject(mem, wp);
        Ellipse(mem, cx - pz(5), cy - pz(5), cx + pz(5), cy + pz(5));
        SelectObject(mem, ob);
        SelectObject(mem, op);
        DeleteObject(wp);
        DeleteObject(bp);
    }

    /* 色相条 */
    if (!P.hue) P.hue = make_hue(pz(HUE_W), pz(SQ_H));
    rect_hue(&r);
    if (P.hue) {
        HDC dc2 = CreateCompatibleDC(mem);
        HGDIOBJ ob = SelectObject(dc2, P.hue);
        BitBlt(mem, r.x, r.y, r.w, r.h, dc2, 0, 0, SRCCOPY);
        SelectObject(dc2, ob);
        DeleteDC(dc2);
    }
    {
        int cy = r.y + P.h * (r.h - 1) / 359;
        HBRUSH wb = CreateSolidBrush(RGB(255, 255, 255));
        HPEN bp = CreatePen(PS_SOLID, 1, RGB(60, 56, 52));
        HGDIOBJ ob = SelectObject(mem, wb), op = SelectObject(mem, bp);
        Ellipse(mem, r.x - pz(3), cy - pz(5), r.x + r.w + pz(3), cy + pz(5));
        SelectObject(mem, ob);
        SelectObject(mem, op);
        DeleteObject(wb);
        DeleteObject(bp);
    }

    /* Hex + 预览 */
    rect_hex(&r);
    draw_hex(mem, &r);
    rect_prev(&r);
    {
        HBRUSH b;
        HPEN p = CreatePen(PS_SOLID, 1, K_BORD);
        HGDIOBJ ob, op;
        cur_rgb(&r8, &g8, &b8);
        b = CreateSolidBrush(RGB(r8, g8, b8));
        ob = SelectObject(mem, b);
        op = SelectObject(mem, p);
        Rectangle(mem, r.x, r.y, r.x + r.w, r.y + r.h);
        SelectObject(mem, ob);
        SelectObject(mem, op);
        DeleteObject(b);
        DeleteObject(p);
    }

    for (i = 0; i < 4; i++) draw_slider(mem, i);
    draw_btn(mem, 1, 0);
    draw_btn(mem, 0, 0);

    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    DeleteObject(mb);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

/* ---------------- 交互 ---------------- */

static void set_from_pt(int x, int y)
{
    RC r;
    if (P.drag == 1) {
        rect_sq(&r);
        P.s = (x - r.x) * 255 / (r.w - 1);
        P.v = 255 - (y - r.y) * 255 / (r.h - 1);
        if (P.s < 0) P.s = 0;
        if (P.s > 255) P.s = 255;
        if (P.v < 0) P.v = 0;
        if (P.v > 255) P.v = 255;
    } else if (P.drag == 2) {
        rect_hue(&r);
        P.h = (y - r.y) * 359 / (r.h - 1);
        if (P.h < 0) P.h = 0;
        if (P.h > 359) P.h = 359;
    } else if (P.drag >= 3) {
        int i = P.drag - 3, val, r8, g8, b8;
        rect_slider(i, &r);
        val = (x - r.x) * 255 / (r.w - 1);
        if (val < 0) val = 0;
        if (val > 255) val = 255;
        if (i == 3) { P.a = val; return; }
        cur_rgb(&r8, &g8, &b8);
        if (i == 0) r8 = val;
        else if (i == 1) g8 = val;
        else b8 = val;
        rgb2hsv(r8, g8, b8, &P.h, &P.s, &P.v);
    }
}

static LRESULT CALLBACK PickProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    switch (msg) {
    case WM_CREATE:
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        on_paint(hwnd);
        return 0;
    case WM_LBUTTONDOWN: {
        RC r;
        SetCapture(hwnd);
        rect_sq(&r);
        if (in_rc(&r, x, y)) { P.drag = 1; set_from_pt(x, y); InvalidateRect(hwnd, NULL, FALSE); return 0; }
        rect_hue(&r);
        if (in_rc(&r, x, y)) { P.drag = 2; set_from_pt(x, y); InvalidateRect(hwnd, NULL, FALSE); return 0; }
        for (int i = 0; i < 4; i++) {
            rect_slider(i, &r);
            r.y -= pz(8); r.h += pz(16);
            if (in_rc(&r, x, y)) {
                P.drag = 3 + i;
                set_from_pt(x, y);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }
        for (int i = 0; i < 8; i++) {
            static const COLORREF SW[8] = {
                RGB(255,255,255), RGB(255,255,0), RGB(0,0,255), RGB(255,140,0),
                RGB(0,200,0), RGB(255,0,0), RGB(1,1,1), RGB(128,128,128)
            };
            rect_sw(i, &r);
            if (in_rc(&r, x, y)) {
                rgb2hsv(GetRValue(SW[i]), GetGValue(SW[i]), GetBValue(SW[i]), &P.h, &P.s, &P.v);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }
        rect_btn(1, &r);
        if (in_rc(&r, x, y)) { g_ok = 1; g_done = 1; DestroyWindow(hwnd); return 0; }
        rect_btn(0, &r);
        if (in_rc(&r, x, y)) { g_done = 1; DestroyWindow(hwnd); return 0; }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (P.drag) { set_from_pt(x, y); InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        P.drag = 0;
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { g_done = 1; DestroyWindow(hwnd); }
        else if (wp == VK_RETURN) { g_ok = 1; g_done = 1; DestroyWindow(hwnd); }
        return 0;
    case WM_CLOSE:
        g_done = 1;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_done = 1;
        free_bm();
        if (P.font)  { DeleteObject(P.font);  P.font = NULL; }
        if (P.fontb) { DeleteObject(P.fontb); P.fontb = NULL; }
        if (P.br_bg) { DeleteObject(P.br_bg); P.br_bg = NULL; }
        P.hwnd = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int colorpick_open(App *a, const WCHAR *title, int *rgb, int *alpha)
{
    WNDCLASSEXW wc;
    MSG msg;
    RECT ar;
    int ww, wh, r, g, b;

    if (!a || !rgb) return 0;
    memset(&P, 0, sizeof(P));
    P.dpi = a->dpi > 0 ? a->dpi : 96;
    P.fs  = a->cfg.font;
    P.font  = pk_font(9, 0);
    P.fontb = pk_font(9, 1);
    r = (*rgb >> 16) & 255; g = (*rgb >> 8) & 255; b = *rgb & 255;
    rgb2hsv(r, g, b, &P.h, &P.s, &P.v);
    P.a = alpha ? *alpha : 255;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PickProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = CLS_PICK;
    RegisterClassExW(&wc);

    ar.left = 0; ar.top = 0; ar.right = pz(400); ar.bottom = pz(474);
    AdjustWindowRectEx(&ar, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOOLWINDOW);
    ww = ar.right - ar.left;
    wh = ar.bottom - ar.top;

    g_done = 0; g_ok = 0;
    P.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, CLS_PICK,
                             title ? title : L"选择颜色",
                             WS_POPUP | WS_CAPTION | WS_SYSMENU,
                             CW_USEDEFAULT, CW_USEDEFAULT, ww, wh,
                             a->hwnd, NULL, GetModuleHandleW(NULL), NULL);
    if (!P.hwnd) return 0;
    ShowWindow(P.hwnd, SW_SHOW);
    UpdateWindow(P.hwnd);
    SetForegroundWindow(P.hwnd);

    while (!g_done && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) { PostQuitMessage((int)msg.wParam); break; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (P.hwnd) DestroyWindow(P.hwnd);
    if (!g_ok) return 0;
    cur_rgb(&r, &g, &b);
    *rgb = (r << 16) | (g << 8) | b;
    if (alpha) *alpha = P.a;
    return 1;
}
