# Windows flasher validation

The portable x64 Windows build embeds Python 3.12, esptool 4.8.1, pyserial and
the hash-verified public 42-face payload. No system Python installation or pip
invocation is used by the app. The normal CLI installer remains supported.

## Automated checks

Thirty installer/flasher tests cover the protected layout and image hashes,
complete backup before writing, incomplete-backup rejection, wrong flash-size
rejection, in-process bundled esptool dispatch and error handling. They also
cover firmware-success retention after SD failure, repair without flashing,
read-only verification, setup serial cleanup and SD transfer/recovery cases.

The frozen EXE performs a no-device payload validation during each build.
GitHub Actions builds the Windows package from the firmware job's verified
artifact, then uploads the Windows ZIP and SHA-256. These are build artifacts,
not an automatically published GitHub Release.

## Physical Orb

- The frozen EXE's connection check detected ESP32-S3 and 16 MB flash. The owner
  also clicked Check connection in the real app and confirmed its success text.
- The final frozen EXE passed payload verification, connection check and the
  Wi-Fi/time setup shortcut with Python removed from PATH and a job folder
  containing spaces and an accented character. Setup was then closed and the
  saved station connection confirmed. A transient reconnect was allowed to
  settle rather than requiring an immediate connected status after reset.
- Its automatic GUI smoke test opened the real bundled Tk interface at 860x650,
  verified the payload, enumerated ports and closed without input. No mouse or
  keyboard control was used for that test.
- A frozen-worker upgrade saved all 16 MB, verified the backup SHA-256, and wrote
  the public bootloader, partition table and firmware using the protected
  installer path. esptool verified the writes.
- A separate NVS readback matched the original 0x9000-0xe000 bytes exactly.
- The exact original personal bootloader/partitions/application were restored
  from that verified backup, excluding NVS and theme storage. The owner's Wi-Fi,
  timezone, clock/group, volume, Favorites, libraries and timer settings matched
  the pre-test snapshot afterward.

The hardware write test intentionally used the internal firmware-only operation
to avoid adding duplicate public SD packs to the owner's personal card. Full SD
installation/repair is covered by the shared uploader's existing hardware
validation and the automated full-flow tests; this run does not claim a separate
factory-fresh, end-to-end Windows/SD installation on a second device.

The package is unsigned and has not been certified on every Windows version.
The target is x64 Windows 10/11. Backups and logs remain local under
`%LOCALAPPDATA%\OrbOfManyFaces`; backups can contain private device settings.

Each EXE bundles one firmware version. Releasing an update means providing a
new EXE/ZIP; automatic update discovery and a browser flasher are future work.
