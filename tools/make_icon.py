"""Generates res/app.ico: a Fluent-style rounded square with a "blocked" glyph."""
from PIL import Image, ImageDraw

SIZE = 1024
img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
draw = ImageDraw.Draw(img)

# Vertical accent gradient, drawn as thin horizontal strips inside a mask.
gradient = Image.new("RGB", (SIZE, SIZE))
top = (0x0A, 0x54, 0xA0)
bottom = (0x2E, 0x8F, 0xE8)
for y in range(SIZE):
    t = y / (SIZE - 1)
    gradient.paste(
        (
            int(top[0] + (bottom[0] - top[0]) * t),
            int(top[1] + (bottom[1] - top[1]) * t),
            int(top[2] + (bottom[2] - top[2]) * t),
        ),
        (0, y, SIZE, y + 1),
    )

radius = int(SIZE * 0.22)
mask = Image.new("L", (SIZE, SIZE), 0)
ImageDraw.Draw(mask).rounded_rectangle([0, 0, SIZE - 1, SIZE - 1], radius=radius, fill=255)
img.paste(gradient, (0, 0), mask)

draw = ImageDraw.Draw(img)
cx = cy = SIZE / 2
ring_r = SIZE * 0.30
ring_w = SIZE * 0.085
draw.ellipse(
    [cx - ring_r, cy - ring_r, cx + ring_r, cy + ring_r],
    outline=(255, 255, 255, 255),
    width=int(ring_w),
)

# Diagonal slash, clipped to the ring.
slash = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
sd = ImageDraw.Draw(slash)
sd.line(
    [(cx - ring_r * 0.72, cy + ring_r * 0.72), (cx + ring_r * 0.72, cy - ring_r * 0.72)],
    fill=(255, 255, 255, 255),
    width=int(ring_w),
)
ring_mask = Image.new("L", (SIZE, SIZE), 0)
ImageDraw.Draw(ring_mask).ellipse(
    [cx - ring_r + ring_w / 2, cy - ring_r + ring_w / 2, cx + ring_r - ring_w / 2, cy + ring_r - ring_w / 2],
    fill=255,
)
img = Image.alpha_composite(img, Image.composite(slash, Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0)), ring_mask))

img.save(
    r"D:\Me\DeepSeek Projects\NoUninstallIcon\res\app.ico",
    sizes=[(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (20, 20), (16, 16)],
)
img.resize((256, 256), Image.LANCZOS).save(
    r"D:\Me\DeepSeek Projects\NoUninstallIcon\build\icon-preview.png"
)
print("icon written")
