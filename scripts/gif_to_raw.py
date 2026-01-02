from PIL import Image
import numpy as np

width, height = 128, 32 # aspect ratio of the pixel art

img = Image.open("input.gif").convert("RGB")
src_w, src_h = img.size

# Compute integer scale factor
scale = min(src_w // width, src_h // height)
if scale < 1:
    raise ValueError("Source image too small")

# Integer downscale = no averaging blur
img = img.resize(
    (src_w // scale, src_h // scale),
    Image.NEAREST
)

# Center crop
new_w, new_h = img.size
left = (new_w - width) // 2
top  = (new_h - height) // 2
img = img.crop((left, top, left + width, top + height))

arr = np.array(img, dtype=np.uint8)
arr.tofile("output.raw")
