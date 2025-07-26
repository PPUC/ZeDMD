from PIL import Image
import numpy as np

# Pixel parameters
width, height = 128, 32 # must match or error
channels = 3  # RGB [R, G, B]

# Load the raw image
with open("ppuc.raw", "rb") as f:
    raw_data = f.read()

# Convert to array
image_array = np.frombuffer(raw_data, dtype=np.uint8).reshape((height, width, channels))

# Convert to display an image and save
img = Image.fromarray(image_array, mode='RGB')
img.save("ppucsplash2.png")
