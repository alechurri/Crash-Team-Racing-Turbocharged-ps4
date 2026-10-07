"""Draws the package icon (512x512): plain text in the project's bundled, freely licensed fonts.

    python make_icon.py <output.png>
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, "..", "assets", "fonts")


def font(name, size):
    try:
        return ImageFont.truetype(os.path.join(FONTS, name), size)
    except OSError:
        return ImageFont.load_default()


img = Image.new("RGB", (512, 512), (20, 34, 74))
d = ImageDraw.Draw(img)
d.rectangle([0, 330, 512, 512], fill=(232, 112, 24))
d.text((256, 170), "CTR", font=font("LuckiestGuy-Regular.ttf", 190), fill=(255, 214, 64), anchor="mm")
d.text((256, 405), "TURBOCHARGED", font=font("FuzzyBubbles-Bold.ttf", 60), fill=(255, 255, 255), anchor="mm")
d.text((256, 470), "PS4", font=font("LuckiestGuy-Regular.ttf", 46), fill=(20, 34, 74), anchor="mm")
img.save(sys.argv[1])
