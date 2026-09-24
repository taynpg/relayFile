"""
生成 relayFile 三个可执行文件的图标：
  - relayFileServer (Server.ico)   蓝色调，中继节点辐射
  - relayFileGui    (Gui.ico)      紫蓝色调，窗口内文件传输
  - relayFileClient (Client.ico)   琥珀色调，终端 + 文件箭头

用 Pillow 绘制矢量几何图形，输出多尺寸 ICO（16/32/48/64/128/256）。
"""

import math
import os
from PIL import Image, ImageDraw, ImageFilter

OUT_DIR = r"e:\codeFinal\relayFile\Gui\Resource"
SIZE = 512  # 高分辨率绘制，再缩放

ICO_SIZES = [(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]


def rounded_rect_mask(size, radius):
    """生成圆角矩形蒙版"""
    mask = Image.new("L", size, 0)
    d = ImageDraw.Draw(mask)
    d.rounded_rectangle([0, 0, size[0] - 1, size[1] - 1], radius=radius, fill=255)
    return mask


def lerp_color(c1, c2, t):
    return tuple(int(a + (b - a) * t) for a, b in zip(c1, c2))


def vertical_gradient(size, top, bottom):
    """垂直渐变背景"""
    img = Image.new("RGBA", size)
    px = img.load()
    h = size[1]
    for y in range(h):
        t = y / (h - 1)
        c = lerp_color(top, bottom, t)
        for x in range(size[0]):
            px[x, y] = c
    return img


def draw_arrow(draw, start, end, width, color):
    """画带箭头的线段"""
    draw.line([start, end], fill=color, width=width)
    # 箭头
    angle = math.atan2(end[1] - start[1], end[0] - start[0])
    ah = width * 3.5
    p1 = (end[0] - ah * math.cos(angle - math.pi / 6),
          end[1] - ah * math.sin(angle - math.pi / 6))
    p2 = (end[0] - ah * math.cos(angle + math.pi / 6),
          end[1] - ah * math.sin(angle + math.pi / 6))
    draw.polygon([end, p1, p2], fill=color)


def draw_document(draw, cx, cy, w, h, fill, outline, lw):
    """画一个带折角的文档图标"""
    # 文档主体
    draw.rounded_rectangle(
        [cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2],
        radius=w * 0.08, fill=fill, outline=outline, width=lw
    )
    # 折角
    fold = w * 0.28
    draw.polygon([
        (cx + w / 2 - fold, cy - h / 2),
        (cx + w / 2, cy - h / 2 + fold),
        (cx + w / 2 - fold, cy - h / 2 + fold),
    ], fill=outline)
    # 文字横线
    line_y = cy - h * 0.05
    for i in range(3):
        ly = line_y + i * h * 0.18
        draw.line([(cx - w * 0.28, ly), (cx + w * 0.18, ly)], fill=outline, width=lw)


def make_server_icon():
    """relayFileServer: 蓝色，中央中继节点 + 辐射箭头 + 底座"""
    s = SIZE
    img = vertical_gradient((s, s), (30, 90, 160), (15, 45, 95))
    # 圆角裁剪
    img = Image.composite(img, Image.new("RGBA", (s, s), (0, 0, 0, 0)),
                          rounded_rect_mask((s, s), int(s * 0.18)))
    d = ImageDraw.Draw(img)

    cx, cy = s / 2, s / 2
    # 外圈光环
    for i, r in enumerate([s * 0.42, s * 0.33]):
        d.ellipse([cx - r, cy - r, cx + r, cy + r],
                  outline=(120, 200, 255, 80), width=3)

    # 四个辐射箭头（上下左右）
    arrow_color = (120, 210, 255, 255)
    inner_r = s * 0.16
    outer_r = s * 0.43
    for ang in (0, 90, 180, 270):
        rad = math.radians(ang)
        sx = cx + inner_r * math.cos(rad)
        sy = cy + inner_r * math.sin(rad)
        ex = cx + outer_r * math.cos(rad)
        ey = cy + outer_r * math.sin(rad)
        draw_arrow(d, (sx, sy), (ex, ey), int(s * 0.03), arrow_color)

    # 中央节点（服务器堆叠方块）
    node_w = s * 0.26
    node_h = s * 0.14
    for i in range(3):
        ny = cy - node_h + i * node_h * 0.9
        d.rounded_rectangle(
            [cx - node_w / 2, ny, cx + node_w / 2, ny + node_h],
            radius=int(s * 0.02), fill=(70, 150, 230), outline=(180, 230, 255), width=int(s * 0.006)
        )
        # 小指示灯
        d.ellipse([cx + node_w * 0.30, ny + node_h * 0.35,
                   cx + node_w * 0.38, ny + node_h * 0.55], fill=(120, 255, 170))

    return img


def make_gui_icon():
    """relayFileGui: 紫蓝，窗口框架 + 文档 + 双向传输箭头"""
    s = SIZE
    img = vertical_gradient((s, s), (95, 60, 180), (50, 30, 110))
    img = Image.composite(img, Image.new("RGBA", (s, s), (0, 0, 0, 0)),
                          rounded_rect_mask((s, s), int(s * 0.18)))
    d = ImageDraw.Draw(img)

    cx, cy = s / 2, s / 2
    # 窗口外框
    ww, wh = s * 0.82, s * 0.74
    wx, wy = cx - ww / 2, cy - wh / 2
    d.rounded_rectangle([wx, wy, wx + ww, wy + wh],
                        radius=int(s * 0.045),
                        fill=(245, 245, 255, 245), outline=(180, 170, 230), width=int(s * 0.014))
    # 标题栏
    th = s * 0.11
    d.rounded_rectangle([wx, wy, wx + ww, wy + th],
                        radius=int(s * 0.045), fill=(220, 210, 250))
    # 三个小圆点
    for i, c in enumerate([(255, 95, 86), (255, 189, 46), (39, 201, 63)]):
        dx = wx + s * 0.045 + i * s * 0.06
        d.ellipse([dx, wy + th / 2 - s * 0.022, dx + s * 0.044, wy + th / 2 + s * 0.022], fill=c)

    # 内容区背景
    d.rounded_rectangle([wx + s * 0.025, wy + th + s * 0.015,
                         wx + ww - s * 0.025, wy + wh - s * 0.025],
                        radius=int(s * 0.025), fill=(255, 255, 255))

    # 两个文档（源/目标）
    doc_w = s * 0.20
    doc_h = s * 0.28
    doc1_cx = cx - s * 0.22
    doc2_cx = cx + s * 0.22
    doc_cy = cy + s * 0.07
    draw_document(d, doc1_cx, doc_cy, doc_w, doc_h, (255, 255, 255), (80, 60, 160), int(s * 0.01))
    draw_document(d, doc2_cx, doc_cy, doc_w, doc_h, (255, 255, 255), (80, 60, 160), int(s * 0.01))

    # 双向传输箭头
    arrow_c = (120, 80, 220, 255)
    ay = doc_cy - s * 0.02
    draw_arrow(d, (doc1_cx + doc_w * 0.55, ay), (doc2_cx - doc_w * 0.55, ay), int(s * 0.022), arrow_c)
    draw_arrow(d, (doc2_cx - doc_w * 0.55, ay + s * 0.09), (doc1_cx + doc_w * 0.55, ay + s * 0.09),
               int(s * 0.022), arrow_c)

    return img


def make_client_icon():
    """relayFileClient: 琥珀色，终端窗口 + > 提示符 + 文档箭头"""
    s = SIZE
    img = vertical_gradient((s, s), (210, 130, 30), (140, 75, 10))
    img = Image.composite(img, Image.new("RGBA", (s, s), (0, 0, 0, 0)),
                          rounded_rect_mask((s, s), int(s * 0.18)))
    d = ImageDraw.Draw(img)

    cx, cy = s / 2, s / 2
    # 终端窗口
    ww, wh = s * 0.82, s * 0.74
    wx, wy = cx - ww / 2, cy - wh / 2
    d.rounded_rectangle([wx, wy, wx + ww, wy + wh],
                        radius=int(s * 0.045),
                        fill=(30, 22, 12, 245), outline=(120, 90, 40), width=int(s * 0.014))
    # 标题栏
    th = s * 0.11
    d.rounded_rectangle([wx, wy, wx + ww, wy + th],
                        radius=int(s * 0.045), fill=(60, 45, 22))
    for i, c in enumerate([(255, 95, 86), (255, 189, 46), (39, 201, 63)]):
        dx = wx + s * 0.045 + i * s * 0.06
        d.ellipse([dx, wy + th / 2 - s * 0.022, dx + s * 0.044, wy + th / 2 + s * 0.022], fill=c)

    # 终端文字
    text_color = (255, 200, 90, 255)
    line_h = s * 0.11
    base_y = wy + th + s * 0.07
    # 提示符行
    d.text((wx + s * 0.06, base_y), ">", fill=(120, 255, 150))
    d.text((wx + s * 0.14, base_y), "relay client", fill=text_color)
    # 模拟命令行
    d.text((wx + s * 0.06, base_y + line_h), "$ send ./data.zip", fill=text_color)
    d.text((wx + s * 0.06, base_y + line_h * 2), "-> 100% (2.4MB)", fill=(150, 230, 150))

    # 右下角小文档 + 箭头（表示客户端也是文件传输端点）
    doc_cx = cx + s * 0.20
    doc_cy = cy + s * 0.18
    draw_document(d, doc_cx, doc_cy, s * 0.17, s * 0.24, (255, 240, 210), (120, 80, 20), int(s * 0.008))
    # 向上箭头
    draw_arrow(d, (doc_cx - s * 0.16, doc_cy + s * 0.06), (doc_cx - s * 0.02, doc_cy - s * 0.05),
               int(s * 0.02), (255, 210, 120, 255))

    return img


def save_as_ico(img, path):
    """保存为多尺寸 ICO"""
    # 先做一次轻微锐化让缩放后更清晰
    img = img.filter(ImageFilter.UnsharpMask(radius=1.5, percent=120, threshold=2))
    img.save(path, format="ICO", sizes=ICO_SIZES)
    print(f"saved: {path}")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    save_as_ico(make_server_icon(), os.path.join(OUT_DIR, "Server.ico"))
    save_as_ico(make_gui_icon(), os.path.join(OUT_DIR, "Gui.ico"))
    save_as_ico(make_client_icon(), os.path.join(OUT_DIR, "Client.ico"))
    print("all icons done.")


if __name__ == "__main__":
    main()
