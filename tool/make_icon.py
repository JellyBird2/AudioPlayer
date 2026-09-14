# Generates src/AudioPlayer.ico (16/32/48/256px): amber quaver on dark
# rounded square. Run: python3 tool/make_icon.py
from PIL import Image, ImageDraw

S = 256
AMBER = (245, 185, 66, 255)
AMBER_DK = (200, 140, 40, 255)
TOP = (64, 50, 112)
BOT = (18, 14, 34)

# --- background: vertical gradient clipped to a rounded square ---
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle([8, 8, S - 8, S - 8], radius=58, fill=255)
col = Image.new("RGB", (1, S))
for y in range(S):
    t = y / (S - 1)
    col.putpixel((0, y), tuple(int(TOP[i] + (BOT[i] - TOP[i]) * t) for i in range(3)))
col = col.resize((S, S))
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
img.paste(col, (0, 0), mask)
d = ImageDraw.Draw(img)
d.rounded_rectangle([8, 8, S - 8, S - 8], radius=58, outline=AMBER, width=9)

# --- note head (rotated ellipse on its own layer) ---
head = Image.new("RGBA", (S, S), (0, 0, 0, 0))
hd = ImageDraw.Draw(head)
hd.ellipse([58, 142, 150, 212], fill=AMBER)
hd.ellipse([72, 152, 132, 200], fill=AMBER_DK)  # inner depth
hd.ellipse([82, 158, 112, 184], fill=(255, 225, 150, 255))  # highlight
head = head.rotate(-18, resample=Image.BICUBIC, center=(104, 177))
img.alpha_composite(head)

# --- stem + flag ---
d = ImageDraw.Draw(img)
d.rectangle([140, 62, 158, 172], fill=AMBER)
d.polygon(
    [(158, 62), (222, 86), (214, 138), (194, 148), (200, 108), (158, 94)],
    fill=AMBER,
)

img.save("src/AudioPlayer.ico", sizes=[(16, 16), (32, 32), (48, 48), (256, 256)])
print("wrote src/AudioPlayer.ico")
im = Image.open("src/AudioPlayer.ico")
print("sizes:", im.info.get("sizes", "?"))
