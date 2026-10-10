#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Regenerate luneos-charger's artwork: assets/logo.png, text.png and text.idx.

Not run at build time - Pillow is not something the initramfs build should
need - so the outputs are committed. Rerun after changing the logo, the fonts,
the sizes or the strings, and commit all three files.

Everything is rendered at ASSET_SCALE times design size (src/render.h: a
1440 x 2400 portrait reference screen), so that luneos-charger, which
resamples it once at start-up, only ever scales it down: a 1440-wide panel
gets it at half size, and nothing up to 2880 wide is blurred by upscaling.

  logo.png   the LuneOS logo, RGBA, LOGO_SIZE * ASSET_SCALE square
  text.png   every string and digit drawn, as 8-bit coverage stacked into
             columns of one atlas; the charger tints and blends them
  text.idx   one line per atlas entry: name x y w h

Digits and '%' are rendered in cells of the font's full ascent + descent and
their advance width, so they line up on a common baseline side by side.

Usage: gen_assets.py <logo> <digit-font.ttf> <text-font.ttf> <outdir>

The committed outputs come from:
  logo   psd/Luneos-logo-1024.psd in https://github.com/webOS-ports/graphics
         (commit be7566c), the LuneOS logo's source art; Pillow reads the
         PSD's flattened composite
  fonts  Prelude, the webOS typeface, from files/conf/fonts/fonts.tgz in
         webOS-ports/luna-init: PreludeWGL-Light.ttf for the percentage (the
         big light numerals of the webOS UI) and Prelude-Medium.ttf for the
         words. Prelude is HP Proprietary Software under the Open webOS
         Software Font License (assets/Prelude_Font_LICENSE.txt), so text.png
         is under that licence too, not under the code's Apache-2.0.

  git clone https://github.com/webOS-ports/graphics
  tar xzf luna-init/files/conf/fonts/fonts.tgz PreludeWGL-Light.ttf Prelude-Medium.ttf
  tools/gen_assets.py graphics/psd/Luneos-logo-1024.psd \
      PreludeWGL-Light.ttf Prelude-Medium.ttf assets
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

# Design sizes: keep in step with LOGO, GLYPH_H and ASSET_SCALE in
# src/render.c.
ASSET_SCALE = 2
LOGO_SIZE = 400 * ASSET_SCALE
DIGIT_PT = 200 * ASSET_SCALE
LABEL_PT = 75 * ASSET_SCALE
HINT_PT = 53 * ASSET_SCALE
ETA_PT = 60 * ASSET_SCALE
COLUMN_H = 4096

STRINGS = [
    ('charging', 'Charging', LABEL_PT),
    ('charged', 'Charged', LABEL_PT),
    ('fast', 'Fast charging', LABEL_PT),
    ('hint', 'Hold the power key to start LuneOS', HINT_PT),
    ('unplugged', 'Charger removed, powering off', HINT_PT),
    ('too_low', 'Battery too low to start', HINT_PT),
]


def text_cov(text, font):
    left, top, right, bottom = font.getbbox(text)
    im = Image.new('L', (right - left + 4, bottom - top + 4), 0)
    ImageDraw.Draw(im).text((2 - left, 2 - top), text, font=font, fill=255)
    return im


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    logo_path, digit_font, text_font, outdir = sys.argv[1:5]

    Image.open(logo_path).convert('RGBA').resize(
        (LOGO_SIZE, LOGO_SIZE), Image.LANCZOS).save(
        os.path.join(outdir, 'logo.png'), optimize=True)

    entries = []
    font = ImageFont.truetype(digit_font, DIGIT_PT)
    ascent, descent = font.getmetrics()
    for ch, name in [(c, c) for c in '0123456789'] + [('%', 'pct')]:
        im = Image.new('L', (int(round(font.getlength(ch))), ascent + descent), 0)
        ImageDraw.Draw(im).text((0, 0), ch, font=font, fill=255)
        entries.append(('glyph_' + name, im))
    # The remaining-time line, composed at run time ("Full in 1 h 20 min")
    # from pieces in cells of one height and their advance widths, so they
    # share a baseline like the big digits.
    small = ImageFont.truetype(text_font, ETA_PT)
    s_asc, s_desc = small.getmetrics()
    for name, text in [('t%d' % i, str(i)) for i in range(10)] + [
            ('t_full_in', 'Full in'), ('t_h', 'h'), ('t_min', 'min'),
            ('t_space', ' ')]:
        im = Image.new('L', (int(round(small.getlength(text))), s_asc + s_desc), 0)
        ImageDraw.Draw(im).text((0, 0), text, font=small, fill=255)
        entries.append((name, im))
    for name, text, pt in STRINGS:
        entries.append((name, text_cov(text, ImageFont.truetype(text_font, pt))))

    # Stack the entries in columns no taller than COLUMN_H, so the atlas stays
    # well inside what the loader accepts (8192 a side).
    places, x, y, col_w, height = [], 0, 0, 0, 0
    for name, im in entries:
        if y and y + im.height > COLUMN_H:
            x, y, col_w = x + col_w + 2, 0, 0
        places.append((name, im, x, y))
        col_w = max(col_w, im.width)
        y += im.height
        height = max(height, y)
    atlas = Image.new('L', (x + col_w, height), 0)
    lines = []
    for name, im, px, py in places:
        atlas.paste(im, (px, py))
        lines.append('%s %d %d %d %d' % (name, px, py, im.width, im.height))
    atlas.save(os.path.join(outdir, 'text.png'), optimize=True)
    with open(os.path.join(outdir, 'text.idx'), 'w') as f:
        f.write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
