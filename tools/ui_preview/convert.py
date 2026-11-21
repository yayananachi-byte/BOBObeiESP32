"""Convert actual LVGL framebuffer captures to PNG and an overview sheet."""
from pathlib import Path
import argparse
from PIL import Image, ImageDraw

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("folder", type=Path, help="The preview executable's screenshots directory")
args = parser.parse_args()
names = ["home", "home_more", "control", "control_50", "control_100", "bluetooth",
         "ball", "motion", "patterns", "settings"]
sheet = Image.new("RGB", (480 * 4, 510 * 3), (30, 25, 30))
draw = ImageDraw.Draw(sheet)
for i, name in enumerate(names):
    image = Image.open(args.folder / (name + ".ppm"))
    image.save(args.folder / (name + ".png"))
    x, y = (i % 4) * 480, (i // 4) * 510
    sheet.paste(image, (x, y + 30))
    draw.text((x + 12, y + 8), name, fill="white")
sheet.save(args.folder / "overview.png")
print(args.folder / "overview.png")
