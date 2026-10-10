#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Regenerate luneos-charger's artwork: assets/logo.png, text.png and text.idx.

Not run at build time - Pillow is not something the initramfs build should
need - so the outputs are committed. Rerun after changing the logo, the font,
the sizes or the strings, and commit all three files.

Everything is rendered at ASSET_SCALE times design size (src/render.h: a
1440 x 2400 portrait reference screen), so that luneos-charger, which
resamples it once at start-up, only ever scales it down: a 1440-wide panel
gets it at half size, and nothing up to 2880 wide is blurred by upscaling.

  logo.png   the LuneOS logo, RGBA, LOGO_SIZE * ASSET_SCALE square
  text.png   every string and digit drawn, as 8-bit coverage stacked into
             one atlas; the charger tints and blends them
  text.idx   one line per atlas entry: name x y w h

Digits and '%' are rendered in cells of the font's full ascent + descent and
their advance width, so they line up on a common baseline side by side.

Usage: gen_assets.py <logo> <font.ttf> <outdir>

The committed outputs come from:
  logo  psd/Luneos-logo-1024.psd in https://github.com/webOS-ports/graphics
        (commit be7566c), the LuneOS logo's source art; Pillow reads the
        PSD's flattened composite
  font  Noto Sans Regular (SIL Open Font License 1.1, which places no
        conditions on rendered output), as in Debian/Ubuntu's fonts-noto-core:
        /usr/share/fonts/truetype/noto/NotoSans-Regular.ttf

  git clone https://github.com/webOS-ports/graphics
  tools/gen_assets.py graphics/psd/Luneos-logo-1024.psd \
      /usr/share/fonts/truetype/noto/NotoSans-Regular.ttf assets
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

STRINGS = [
    ('charging', 'Charging', LABEL_PT),
    ('charged', 'Charged', LABEL_PT),
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
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    logo_path, font_path, outdir = sys.argv[1:4]

    Image.open(logo_path).convert('RGBA').resize(
        (LOGO_SIZE, LOGO_SIZE), Image.LANCZOS).save(
        os.path.join(outdir, 'logo.png'), optimize=True)

    entries = []
    font = ImageFont.truetype(font_path, DIGIT_PT)
    ascent, descent = font.getmetrics()
    for ch, name in [(c, c) for c in '0123456789'] + [('%', 'pct')]:
        im = Image.new('L', (int(round(font.getlength(ch))), ascent + descent), 0)
        ImageDraw.Draw(im).text((0, 0), ch, font=font, fill=255)
        entries.append(('glyph_' + name, im))
    for name, text, pt in STRINGS:
        entries.append((name, text_cov(text, ImageFont.truetype(font_path, pt))))

    width = max(im.width for _, im in entries)
    atlas = Image.new('L', (width, sum(im.height for _, im in entries)), 0)
    lines, y = [], 0
    for name, im in entries:
        atlas.paste(im, (0, y))
        lines.append('%s 0 %d %d %d' % (name, y, im.width, im.height))
        y += im.height
    atlas.save(os.path.join(outdir, 'text.png'), optimize=True)
    with open(os.path.join(outdir, 'text.idx'), 'w') as f:
        f.write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
