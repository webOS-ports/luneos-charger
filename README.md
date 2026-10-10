# luneos-charger

The off-mode charging screen for LuneOS on Halium devices, run from the
initramfs.

Plugging a charger into a powered-off phone makes the bootloader start the
kernel in *charger mode* (`androidboot.mode=charger` and its vendor variants).
Halium's initramfs script answers that by handing the boot to the Android
image's own init for Android's charger UI, which a LuneOS image cannot run:
the BlackBerry KEY2 sat on the bootloader logo for as long as it was plugged
in. `initramfs-scripts-halium` asks `luneos-charger --detect` instead, and on a
charger boot runs `luneos-charger`, which shows:

- the LuneOS logo, a battery filling to the charge level with a band sweeping
  up from it, and the same percentage LuneOS's status bar shows (batteryd's
  `getUiPercent()`);
- the notification LED, where there is one, red while charging and green when
  charged.

| Event | Result |
|---|---|
| no input for 10 s | screen off |
| power key pressed, screen off | screen on |
| short press, screen on | screen off |
| power key held 2 s | exit 10: init.sh boots LuneOS (refused, with a message, below `--min-capacity`) |
| charger unplugged for 10 s | exit 20: init.sh powers off (plugging back in cancels) |
| charger never seen online within 15 s | exit 20 |
| no power key at all | exit 1: init.sh boots normally |

Without a usable display it carries on headless (LED, power key, unplug).

## How other distributions do it

| | Detects | Shows | Where |
|---|---|---|---|
| Ubuntu Touch | systemd generator → `charger.target` | Android's `/system/bin/charger` in the LXC container | rootfs |
| Sailfish OS | `preinit` → DSME `ACT_DEAD` | `jolla-actdead-charging` (QML) | rootfs, reduced target |
| Droidian | initramfs `identify_boot_mode()` | nothing: `reboot -f` | initramfs |
| FuriLabs (FLX1) | initramfs `mountroot()` | `lvglcharger` (LVGL over minui) | initramfs |
| postmarketOS | — | nothing since 2023 (`charging-sdl` removed) | — |

LuneOS follows the FuriLabs route: on Halium, anything in the rootfs needs the
Android container and hwcomposer just to draw, and the container's own charger
fights the compositor for DRM master. The initramfs has the panel to itself.

## Display

Two backends, the same two Android's charger and recovery use (minui):

- **DRM/KMS** dumb buffers with page flips (SDE and MediaTek DRM kernels),
  through the uapi headers rather than libdrm;
- **fbdev** with double buffering by panning (MDSS kernels: msm8916, msm8953,
  msm8994, sdm660), 16, 24 and 32 bpp.

The screen is designed on a 1440×2400 reference canvas (2560×1440 in
landscape) and scaled to fit; the artwork is pre-rendered at that size and
resampled once at start-up. `--rotate` turns it for panels mounted sideways,
`--static` stops the animation for E Ink.

## Options

```
luneos-charger --detect                  exit 0 on a charger boot, 1 otherwise
luneos-charger [--display auto|drm|fbdev|none] [--rotate 0|90|180|270] [--static]
               [--timeout ms] [--hold ms] [--unplug ms] [--discovery ms]
               [--min-capacity n] [--backlight file --brightness n]
               [--led-red dir --led-green dir --led-blue dir | --no-leds]
               [--assets dir] [--level n] [--root dir]
```

`no_charger_mode` on the kernel cmdline makes `--detect` say "normal boot".

## Building and testing

```
meson setup build -Dtests=enabled
ninja -C build
meson test -C build
```

The tests (googletest) run the program's logic against fake `/sys` and
`/proc` trees: charger-boot detection, power-supply and LED discovery, the
policy state machine, PNG loading and resampling, the layout on panels from
320×480 to 1440×3200, and pixel packing and rotation.

`tools/gen_assets.py` regenerates `assets/` from the logo's PSD in
webOS-ports/graphics and Noto Sans (needs Pillow; not run at build time -
see the script for the exact command).

## Licences

The code is Apache-2.0 (`LICENSE`). `logo.png` is rendered from the LuneOS
logo's source art, `psd/Luneos-logo-1024.psd` in
[webOS-ports/graphics](https://github.com/webOS-ports/graphics); the same logo
ships as a PNG in
[org.webosports.app.photos](https://github.com/webOS-ports/org.webosports.app.photos)
under Apache-2.0. `text.png` is text rendered from Noto Sans (SIL Open Font
License 1.1, which places no conditions on rendered output).

The first version was written by alan-morford for the BlackBerry KEY2
(meta-smartphone PR #6); this repository starts from that code.
