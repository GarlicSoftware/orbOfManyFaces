# Orb of Many Faces

A lightweight clock-only firmware for the Waveshare **ESP32-S3-Touch-AMOLED-1.75**
Orb: swipe through clocks, turn the knob for volume, and hear a bell on the hour.

**[Download the installer ZIP from Releases](https://github.com/GarlicSoftware/orbOfManyFaces/releases/latest).**
Use the ZIP attached under **Assets**. GitHub's **Code → Download ZIP** downloads
source code and does not include the compiled installer images.

![Meridian Midnight](docs/meridian-midnight.png)

## Controls

| Action | Result |
|---|---|
| Swipe horizontally | Next/previous face in the selected group |
| Turn the knob | Tick and hourly-bell volume; zero mutes both |
| Hold the knob about 1½ seconds | Open the group menu |
| Turn in the menu, then press | Choose a group |
| Hold again, or wait 30 seconds | Close the menu |

Version **0.5.0** includes eight built-in faces and 34 additional CC0 clock faces
in four SD groups: **42 distinct designs**. Blue Meridian is first in every SD
group and remains available without a card. The other Meridian face is Brass &
Parchment. Group, face and volume are remembered after restart.

Groups preload compressed artwork and fonts into RAM when selected. Normal
ticks and face switches do not read SD. Selecting a group takes a few seconds.
Complex oversized rotating layers use cheaper sampling to keep up with the clock.
Animated source backgrounds use their still image.

The hourly bell is one short bell at the start of each local hour, rather than
a count of the hour. It replaces that second's tick. It does not ring on startup
or replay hours after time synchronization. A short grace window covers group
loading across the hour. Volume zero mutes the bell and ticks together.

## Hardware and preparation

Use the Waveshare **ESP32-S3-Touch-AMOLED-1.75**, with 466×466 CO5300 display,
16 MB flash, 8 MB PSRAM, CST9217 touch and ES8311 audio. Similar round displays
and enclosures can contain different boards and require a firmware port.

First use the original [Orb OS](https://github.com/Ziplock78/orb-os) to connect
the recipient's own Wi-Fi and configure their timezone. This firmware reuses
those settings and the hardware RTC. It has no Wi-Fi setup screen. If no timezone
is stored, its fallback is US Eastern; a USB override is available, for example
`?clock timezone UTC0` at 115200 baud. Existing brightness/orientation are retained.

## Install the Release ZIP

1. Install Python 3, then `python -m pip install esptool==4.8.6`.
2. Extract the Release ZIP and open a terminal inside its extracted folder.
3. Run `python install.py --check` to verify image hashes without hardware access.
4. Connect USB, close other serial programs, then run `python install.py --port COM7`.

Replace `COM7` with the actual device port. Linux commonly uses `/dev/ttyACM0`;
macOS commonly uses `/dev/cu.usbmodem...`. If necessary, hold BOOT, tap RESET,
release BOOT, and try again.

The installer saves a complete **recipient-device 16 MB backup** before writing.
It checks the backup size, then writes the supplied bootloader, partition table
and app at their explicit addresses. It does not whole-chip erase or overwrite
NVS/settings, themeart or the SD card. The backup contains the recipient's own
settings; keep it private. `python install.py --port COM7 --dry-run` shows the
commands without accessing hardware.

To restore, substitute the actual backup path and port:

```text
python -m esptool --chip esp32s3 --port COM7 write_flash 0x0 orb-backups/DATE-TIME/orb-before-install.bin
```

## Add the included SD groups

The eight built-in clocks work immediately. For the other 34 faces, copy the
`.ocg` files listed in `sd-groups/index.json` into **`/clock-groups/` at the root
of a FAT32 SD card**. Leave existing `/themes/` and other files in place; no
formatting is needed to add groups to a compatible existing card. Open the knob
menu to discover the groups.

Alternatively, copy them through the connected Orb's USB port:

```text
python -m pip install pyserial==3.5 Pillow
python tools/upload_groups.py --port COM7
```

Uploads use new filenames and refuse overwrites. Interrupted `.part` files do
not appear in the menu. Missing, corrupt or oversized groups fall back to the
built-in clocks. A group already cached in RAM continues working if SD is removed.

## Source and new faces

This repository includes generated `src/assets.cpp`, so rebuilding needs neither
Windows fonts nor Orb Studio:

```text
python -m pip install platformio==6.2.0
python -m platformio run -e orb-clock
python tools/package_release.py
```

The resulting Release ZIP appears under `releases/`. Build dependencies and
versions are pinned in `platformio.ini`. Only the application at `0x10000` is
needed to update an already compatible clock installation; the packaged installer
also provides first-install images and a backup step.

`tools/build_sd_groups.py slug-one slug-two` builds RAM-bounded groups from
folders under `imported-faces/`. With no arguments it builds the included public
library. It converts only clock artwork/fonts/styles. Downloaded Orb Studio theme
files must first be extracted into that folder layout; ensure you have permission
before redistributing additional artwork. `tools/bake_assets.py` regenerates the
built-in art and needs Windows Arial Narrow/Consolas; ordinary firmware builds
use the generated assets already committed here.

USB diagnostics include `?clock status`, `groups`, `group N`, `face N`,
`volume 0..100`, `chime status`, `chime test` and `reboot`. The bell test respects
volume zero. Framebuffer capture is a maintenance feature; large legacy USB
captures can truncate on some hosts without affecting the display.

## Credits and licensing

Firmware and the two Meridian designs are MIT-licensed; preserve [LICENSE](LICENSE).
Board protocols and codec setup derive from Quique Tortosa and Zion Brock's
[Orb OS](https://github.com/Ziplock78/orb-os). See [NOTICE.md](NOTICE.md) for dependencies.

Community faces included here declare **CC0 1.0** in their original manifests.
[ARTWORK_CREDITS.json](ARTWORK_CREDITS.json) preserves their authors and source
slugs; original PNGs, styles and exported fonts are included. Artwork was cropped,
converted to RGB565/alpha, and compressed for this renderer. The public bundle
excludes themes marked all-rights-reserved, unspecified, or other Creative Commons
licenses; those may have different terms. The firmware's MIT license is not a
license grant for arbitrary themes downloaded elsewhere.
