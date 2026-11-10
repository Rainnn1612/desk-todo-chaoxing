/* image.c - 图片解码与等比绘制。
 *
 * 只为了「详情窗里看一眼作业截图」这一件事，所以：
 *   - GDI+ 是延迟启动的，第一次真要显示图片时才 GdiplusStartup；
 *   - 图片对象由详情窗自己持有，关窗就 Dispose，不缓存、不常驻；
 *   - 没人打开详情窗时，这个文件里的代码一行都不会跑。
 */
#include "app.h"
#include <propidl.h>        /* gdiplusflat.h 要用 PROPID，MinGW 的头不会自己带进来 */
#include <gdiplus.h>

struct Img {
    GpImage *g;
    int      w, h;
};

static ULONG_PTR g_token;
static int       g_started;

static int ensure_started(void)
{
    GdiplusStartupInput si;
    if (g_started) return 1;
    memset(&si, 0, sizeof(si));
    si.GdiplusVersion = 1;
    si.SuppressBackgroundThread = FALSE;
    si.SuppressExternalCodecs = FALSE;
    if (GdiplusStartup(&g_token, &si, NULL) != Ok) return 0;
    g_started = 1;
    return 1;
}

int img_ready(void)
{
    return g_started;
}

Img *img_open(const WCHAR *path)
{
    Img *im;
    GpImage *g = NULL;
    UINT w = 0, h = 0;

    if (!path || !path[0]) return NULL;
    if (!ensure_started()) return NULL;
    if (GdipLoadImageFromFile(path, &g) != Ok || !g) return NULL;
    if (GdipGetImageWidth(g, &w) != Ok || GdipGetImageHeight(g, &h) != Ok ||
        w == 0 || h == 0) {
        GdipDisposeImage(g);
        return NULL;
    }
    im = (Img *)calloc(1, sizeof(Img));
    if (!im) {
        GdipDisposeImage(g);
        return NULL;
    }
    im->g = g;
    im->w = (int)w;
    im->h = (int)h;
    return im;
}

void img_close(Img *im)
{
    if (!im) return;
    if (im->g) GdipDisposeImage(im->g);
    free(im);
}

int img_width(Img *im)
{
    return im ? im->w : 0;
}

int img_height(Img *im)
{
    return im ? im->h : 0;
}

/* 等比缩放到 box 内（留 pad 边距），居中绘制 */
void img_draw_fit(Img *im, HDC dc, const RECT *box, int pad)
{
    GpGraphics *g = NULL;
    int bw, bh, dw, dh, x, y;
    double sx, sy, s;

    if (!im || !im->g || !dc || !box) return;
    bw = (box->right - box->left) - pad * 2;
    bh = (box->bottom - box->top) - pad * 2;
    if (bw <= 0 || bh <= 0) return;

    sx = (double)bw / (double)im->w;
    sy = (double)bh / (double)im->h;
    s = sx < sy ? sx : sy;
    dw = (int)((double)im->w * s + 0.5);
    dh = (int)((double)im->h * s + 0.5);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    x = box->left + ((box->right - box->left) - dw) / 2;
    y = box->top + ((box->bottom - box->top) - dh) / 2;

    if (GdipCreateFromHDC(dc, &g) != Ok || !g) return;
    GdipSetInterpolationMode(g, InterpolationModeHighQualityBicubic);
    GdipSetPixelOffsetMode(g, PixelOffsetModeHighQuality);
    GdipSetSmoothingMode(g, SmoothingModeHighQuality);
    /* 先把底铺一层浅色，透明 PNG 也不会糊成一团 */
    {
        GpSolidFill *br = NULL;
        ARGB col = 0xFFFFFFFF;
        if (GdipCreateSolidFill(col, &br) == Ok && br) {
            GdipFillRectangleI(g, (GpBrush *)br, x, y, dw, dh);
            GdipDeleteBrush((GpBrush *)br);
        }
    }
    GdipDrawImageRectI(g, im->g, x, y, dw, dh);
    GdipDeleteGraphics(g);
}

void img_shutdown(void)
{
    if (!g_started) return;
    GdiplusShutdown(g_token);
    g_token = 0;
    g_started = 0;
}
