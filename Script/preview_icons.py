"""把生成的 ICO 渲染成 PNG 供预览"""
from PIL import Image
import os

OUT = r"e:\codeFinal\relayFile\Gui\Resource"
names = ["Server.ico", "Gui.ico", "Client.ico"]
for n in names:
    p = os.path.join(OUT, n)
    img = Image.open(p)
    # 取最大尺寸
    sizes = img.info.get("sizes", [])
    if sizes:
        img.size = max(sizes)
    preview = img.resize((256, 256), Image.LANCZOS)
    out = os.path.join(OUT, n.replace(".ico", "_preview.png"))
    preview.save(out)
    print(f"saved preview: {out}")
