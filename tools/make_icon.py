#!/usr/bin/env python3
# 生成 res/app.ico：圆角雾青底 + 白色对勾，纯标准库手写 ICO（无 Pillow 依赖）。
import struct, sys, os

ACCENT = (0x6F, 0x8C, 0x86)   # 莫兰迪雾青，与 theme.c 的 C_ACCENT 同族
WHITE  = (0xFF, 0xFF, 0xFF)


def in_rrect(x, y, w, r):
    """点 (x,y) 是否落在边长 w、圆角半径 r 的圆角正方形内。"""
    if x < r and y < r:
        return (x - r) ** 2 + (y - r) ** 2 <= r * r
    if x > w - r and y < r:
        return (x - (w - r)) ** 2 + (y - r) ** 2 <= r * r
    if x < r and y > w - r:
        return (x - r) ** 2 + (y - (w - r)) ** 2 <= r * r
    if x > w - r and y > w - r:
        return (x - (w - r)) ** 2 + (y - (w - r)) ** 2 <= r * r
    return 0.0 <= x <= w and 0.0 <= y <= w


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    if L2 <= 1e-12:
        return ((px - ax) ** 2 + (py - ay) ** 2) ** 0.5
    t = ((px - ax) * dx + (py - ay) * dy) / L2
    if t < 0: t = 0.0
    if t > 1: t = 1.0
    qx, qy = ax + t * dx, ay + t * dy
    return ((px - qx) ** 2 + (py - qy) ** 2) ** 0.5


def render(w, ss):
    """超采样渲染，返回 list of (r,g,b,a) 行优先，尺寸 w*w。"""
    W = w * ss
    inv = 1.0 / ss
    rad = 0.22 * W
    thick = 0.105 * W
    # 对勾折线（归一化到 0..W）
    pts = [(0.245 * W, 0.525 * W), (0.435 * W, 0.715 * W), (0.770 * W, 0.315 * W)]

    acc = [[[0.0, 0.0, 0.0, 0.0] for _ in range(w)] for _ in range(w)]
    for sy in range(W):
        fy = sy + 0.5
        for sx in range(W):
            fx = sx + 0.5
            if not in_rrect(fx, fy, W, rad):
                continue
            col = ACCENT
            d = min(seg_dist(fx, fy, *pts[0], *pts[1]),
                    seg_dist(fx, fy, *pts[1], *pts[2]))
            if d <= thick:
                col = WHITE
            cell = acc[sy // ss][sx // ss]
            cell[0] += col[0]
            cell[1] += col[1]
            cell[2] += col[2]
            cell[3] += 255.0

    n = float(ss * ss)
    out = []
    for y in range(w):
        row = []
        for x in range(w):
            r, g, b, a = acc[y][x]
            cov = a / (255.0 * n)
            if cov <= 0.0:
                row.append((0, 0, 0, 0))
            else:
                row.append((int(r / n + 0.5), int(g / n + 0.5), int(b / n + 0.5),
                            int(cov * 255.0 + 0.5)))
        out.append(row)
    return out


def bmp_payload(px, w):
    """BITMAPINFOHEADER + 自下而上的 BGRA 像素 + 全零 AND 掩码。"""
    hdr = struct.pack('<IiiHHIIiiII', 40, w, w * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    data = bytearray()
    for y in range(w - 1, -1, -1):
        for x in range(w):
            r, g, b, a = px[y][x]
            data += bytes((b, g, r, a))
    stride = ((w + 31) // 32) * 4
    data += bytes(stride * w)
    return hdr + bytes(data)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'res/app.ico'
    sizes = [(16, 4), (24, 4), (32, 4), (48, 4), (64, 4), (128, 3), (256, 3)]
    imgs = [bmp_payload(render(w, ss), w) for (w, ss) in sizes]

    d = os.path.dirname(os.path.abspath(out))
    if d and not os.path.isdir(d):
        os.makedirs(d)

    head = struct.pack('<HHH', 0, 1, len(sizes))
    offset = len(head) + 16 * len(sizes)
    entries = bytearray()
    for (w, ss), blob in zip(sizes, imgs):
        dim = 0 if w >= 256 else w
        entries += struct.pack('<BBBBHHII', dim, dim, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    with open(out, 'wb') as f:
        f.write(head + bytes(entries) + b''.join(imgs))
    print('wrote %s (%d bytes, %d sizes)' % (out, offset, len(sizes)))


if __name__ == '__main__':
    main()
