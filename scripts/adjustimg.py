import numpy as np

# Image parameters
width, height = 256, 64
channels = 3  # RGB

# Load the raw image
with open("ppucHD.raw", "rb") as f:
    raw_data = f.read()

# Convert to array
image_array = np.frombuffer(raw_data, dtype=np.uint8).reshape((height, width, channels)).copy()
print(image_array[:, 0])
# Edit first column
image_array[:, 1, 2] = 0

# Save back to a new raw file
image_array.tofile("ppuc_modifiedHD.raw")