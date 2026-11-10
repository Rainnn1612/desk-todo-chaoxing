/* theme.c - 调色板、字体、DPI 度量 */
#include "app.h"

/* 莫兰迪配色：低饱和、灰调、暖中性色。深色为暖炭灰，浅色为暖纸白。 */
static const COLORREF PAL_DARK[C_COUNT] = {
    RGB(46, 44, 42),     /* C_BG      暖炭灰底   #2E2C2A */
    RGB(74, 71, 68),     /* C_BORDER  灰褐描边   #4A4744 */
    RGB(56, 54, 51),     /* C_HEAD    略亮标题条 #383633 */
    RGB(232, 228, 222),  /* C_TEXT    暖白       #E8E4DE */
    RGB(163, 156, 148),  /* C_MUTED   暖灰       #A39C94 */
    RGB(60, 57, 54),     /* C_HOVER   悬停底     #3C3936 */
    RGB(206, 139, 126),  /* C_OVERDUE 陶土玫瑰   #CE8B7E */
    RGB(216, 164, 127),  /* C_TODAY   灰杏       #D8A47F */
    RGB(201, 184, 126),  /* C_SOON    灰卡其     #C9B87E */
    RGB(163, 156, 148),  /* C_NORMAL  暖灰       #A39C94 */
    RGB(122, 116, 109),  /* C_DONE    暗灰       #7A746D */
    RGB(143, 166, 160),  /* C_ACCENT  雾青       #8FA6A0 */
    RGB(107, 101, 94),   /* C_BOX     复选框描边 #6B655E */
    RGB(38, 36, 34)      /* C_EDITBG  编辑框底   #262422 */
};

static const COLORREF PAL_LIGHT[C_COUNT] = {
    RGB(242, 239, 234),  /* C_BG      暖纸白     #F2EFEA */
    RGB(220, 214, 206),  /* C_BORDER  浅灰褐     #DCD6CE */
    RGB(232, 227, 220),  /* C_HEAD    标题条     #E8E3DC */
    RGB(58, 55, 51),     /* C_TEXT    暖墨       #3A3733 */
    RGB(140, 133, 124),  /* C_MUTED   灰褐       #8C857C */
    RGB(230, 224, 216),  /* C_HOVER   悬停底     #E6E0D8 */
    RGB(169, 106, 95),   /* C_OVERDUE 陶土玫瑰   #A96A5F */
    RGB(180, 126, 82),   /* C_TODAY   灰杏       #B47E52 */
    RGB(143, 124, 72),   /* C_SOON    灰卡其     #8F7C48 */
    RGB(140, 133, 124),  /* C_NORMAL  灰褐       #8C857C */
    RGB(176, 170, 162),  /* C_DONE    浅灰       #B0AAA2 */
    RGB(126, 154, 148),  /* C_ACCENT  雾青       #7E9A94 */
    RGB(195, 188, 179),  /* C_BOX     复选框描边 #C3BCB3 */
    RGB(251, 249, 246)   /* C_EDITBG  编辑框底   #FBF9F6 */
};

int sc(App *a, int v) { return MulDiv(v, a->dpi, 96); }

/* 界面骨架尺寸 = DPI 缩放 × 字号百分比。字号放大时行高、标题条、复选框、
   内边距一起放大，窗口整体等比变大，不会出现字大了但行距没跟上的挤压。 */
int fz(App *a, int v)
{
    int k = a->cfg.font;
    if (k < FONT_MIN) k = FONT_MIN;
    if (k > FONT_MAX) k = FONT_MAX;
    return MulDiv(sc(a, v), k, 100);
}

static COLORREF mixc(COLORREF a, COLORREF b, int pct)
{
    int r = GetRValue(a) + (GetRValue(b) - GetRValue(a)) * pct / 100;
    int g = GetGValue(a) + (GetGValue(b) - GetGValue(a)) * pct / 100;
    int bl = GetBValue(a) + (GetBValue(b) - GetBValue(a)) * pct / 100;
    return RGB(r, g, bl);
}

static COLORREF dc(int v) { return RGB((v >> 16) & 255, (v >> 8) & 255, v & 255); }

COLORREF theme_color(App *a, int idx)
{
    if (idx < 0 || idx >= C_COUNT) idx = 0;
    if (a->cfg.theme == 2) {
        /* 自定义配色：三个主色用用户选的，其余从底色/字色推出来，保持层级关系 */
        COLORREF bg   = dc(a->cfg.cl_page);
        COLORREF bar  = dc(a->cfg.cl_bar);
        COLORREF txt  = dc(a->cfg.cl_text);
        COLORREF base = PAL_DARK[idx];
        switch (idx) {
        case C_BG:      return bg;
        case C_HEAD:    return bar;
        case C_TEXT:    return txt;
        case C_MUTED:   return mixc(bg, txt, 55);
        case C_NORMAL:  return mixc(bg, txt, 55);
        case C_DONE:    return mixc(bg, txt, 32);
        case C_HOVER:   return mixc(bg, bar, 55);
        case C_BORDER:  return mixc(bg, txt, 22);
        case C_BOX:     return mixc(bg, txt, 40);
        case C_EDITBG:  return mixc(bg, txt, 8);
        default:        return base;    /* 逾期/今天/即将/强调色沿用莫兰迪那套 */
        }
    }
    return a->cfg.theme ? PAL_LIGHT[idx] : PAL_DARK[idx];
}

void theme_defaults(App *a)
{
    a->cfg.cl_page  = 0x2E2C2A;
    a->cfg.cl_bar   = 0x383633;
    a->cfg.cl_text  = 0xE8E4DE;
    a->cfg.cl_glass = 0x1E1E1E;
    a->cfg.glass_a  = 200;
}

/* 玻璃磨砂：Win10/11 的亚克力模糊（SetWindowCompositionAttribute 是未公开 API，
   用 GetProcAddress 动态取，取不到就安静地退回普通窗口）。背景用色键抠掉，
   这样模糊层能透上来、文字和描边仍然是实心的。 */
void theme_apply_glass(App *a)
{
    typedef struct { int AccentState; int AccentFlags; DWORD GradientColor; int AnimationId; } ACCENT;
    typedef struct { int Attribute; int SizeOfData; void *Data; } WINCOMP;
    typedef BOOL (WINAPI *pSetWCA)(HWND, void *);
    static pSetWCA set_wca = NULL;
    static int tried = 0;
    LONG_PTR ex;

    if (!a || !a->hwnd) return;
    if (!tried) {
        HMODULE u = GetModuleHandleW(L"user32.dll");
        FARPROC f = u ? GetProcAddress(u, "SetWindowCompositionAttribute") : NULL;
        memcpy(&set_wca, &f, sizeof(set_wca));
        tried = 1;
    }
    ex = GetWindowLongPtrW(a->hwnd, GWL_EXSTYLE);
    if (a->cfg.glass) {
        ACCENT ac;
        WINCOMP wc;
        int r = (a->cfg.cl_glass >> 16) & 255;
        int g = (a->cfg.cl_glass >> 8) & 255;
        int b = a->cfg.cl_glass & 255;
        int al = a->cfg.glass_a;
        if (al < 30) al = 30;
        if (al > 255) al = 255;
        SetWindowLongPtrW(a->hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        /* 整体半透明而不是色键抠图：色键会让背景全透明，壁纸花时正文读不清。
           A 通道直接当不透明度用，所以「玻璃配色」的透明度滑块就是它。 */
        SetLayeredWindowAttributes(a->hwnd, 0, (BYTE)al, LWA_ALPHA);
        if (set_wca) {
            ac.AccentState = 4;               /* ACCENT_ENABLE_ACRYLICBLURBEHIND */
            ac.AccentFlags = 2;
            ac.GradientColor = ((DWORD)al << 24) | (b << 16) | (g << 8) | r;   /* ABGR */
            ac.AnimationId = 0;
            ZeroMemory(&wc, sizeof(wc));
            wc.Attribute = 19;                /* WCA_ACCENT_POLICY */
            wc.SizeOfData = sizeof(ac);
            wc.Data = &ac;
            set_wca(a->hwnd, &wc);
        }
    } else {
        if (set_wca) {
            ACCENT ac;
            WINCOMP wc;
            ZeroMemory(&ac, sizeof(ac));
            ac.AccentState = 0;               /* ACCENT_DISABLED */
            ZeroMemory(&wc, sizeof(wc));
            wc.Attribute = 19;
            wc.SizeOfData = sizeof(ac);
            wc.Data = &ac;
            set_wca(a->hwnd, &wc);
        }
        SetLayeredWindowAttributes(a->hwnd, 0, 255, LWA_ALPHA);
        SetWindowLongPtrW(a->hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
    }
    SetWindowPos(a->hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    InvalidateRect(a->hwnd, NULL, FALSE);
}

static HFONT mkfont(App *a, int pt, int weight, int strike, const WCHAR *face)
{
    int k = a->cfg.font;
    int q;
    if (k < FONT_MIN) k = FONT_MIN;
    if (k > FONT_MAX) k = FONT_MAX;
    q = MulDiv(pt * 4, k, 100);           /* 用四分之一磅计算，9pt×85% 不会被抹成 7pt */
    if (q < 20) q = 20;                   /* 下限 5pt，再小就没法看了 */
    return CreateFontW(-MulDiv(q, a->dpi, 72 * 4), 0, 0, 0, weight, 0, 0, strike,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

void theme_free(App *a)
{
    if (a->f_title)  { DeleteObject(a->f_title);  a->f_title = NULL; }
    if (a->f_small)  { DeleteObject(a->f_small);  a->f_small = NULL; }
    if (a->f_icon)   { DeleteObject(a->f_icon);   a->f_icon = NULL; }
    if (a->f_strike) { DeleteObject(a->f_strike); a->f_strike = NULL; }
}

void theme_init(App *a)
{
    a->dpi = 96;
    if (a->hwnd) {
        HMODULE u = GetModuleHandleW(L"user32.dll");
        if (u) {
            FARPROC f = GetProcAddress(u, "GetDpiForWindow");
            if (f) {
                UINT (WINAPI *fn)(HWND);
                UINT d;
                memcpy(&fn, &f, sizeof(fn));
                d = fn(a->hwnd);
                if (d >= 72 && d <= 480) a->dpi = (int)d;
            }
        }
    }
    theme_free(a);
    a->f_title  = mkfont(a, 10, FW_SEMIBOLD, 0, L"Microsoft YaHei UI");
    a->f_small  = mkfont(a, 9,  FW_NORMAL,   0, L"Microsoft YaHei UI");
    a->f_icon   = mkfont(a, 11, FW_NORMAL,   0, L"Segoe UI");
    a->f_strike = mkfont(a, 10, FW_NORMAL,   1, L"Microsoft YaHei UI");
    ui_metrics(a);
}
