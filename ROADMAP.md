# Development roadmap

## 1. Favorites groups (in development)

Short knob press toggles the current face without navigating. Save each change
immediately and independently of generated artwork. A small star appears for two
seconds on bookmarked faces and fades; add/remove operations have matching feedback.
Normal clock rendering and cached swipes must remain fast.

Changes require an explicit rebuild. On opening the library menu, offer Build Now
or Later. Later retains existing groups, persists that dismissal for this revision,
and exposes Update Favorites. Further changes allow a new prompt.

Build ordinary .ocg packs on the Orb, reusing compressed artwork and fonts from
installed libraries and built-in faces. Partition by actual packed bytes, peak
decoded image RAM, font metadata and format limits. Repack/consolidate from scratch
when approved. Show progress and validate every replacement before atomically
switching the library catalog. A failed/interrupted rebuild retains the previous
catalog and independent selections. Favorites preload into RAM like other groups.
Favorites contain only selected faces, with no automatic Blue Meridian. No original
library is modified.

Keep content-based identities and source references compatible with future custom
imports. Missing originals must produce a recoverable error, not silently drop a
bookmark. Favorites require a writable SD card. Settings and unrelated card files
remain untouched.

### Favorites names and long labels

Assign display names automatically in group order: Favorites; More Favorites;
More More Favorites; Too Many Favorites; More Too Many Favorites; We Need to Talk.
Continue with increasingly ridiculous names if more groups are needed. Recompute
these labels after consolidation. Internal identifiers remain independent and
stable (favorites/1, favorites/2, etc.). Keep full names readable on the 466x466
round display: scroll an overflowing selected label unobtrusively instead of
truncating it. This changes labels only, not bookmark behavior, rebuild approvals
or memory-based splitting.

## 2. Custom Clock Face Importer (next)

Standalone Windows GUI for importing existing Orb Studio theme downloads/folders;
this is not a face editor. Reuse Python conversion, .ocg packing and verified USB
transfer. Validate artwork/configuration, RAM and supported effects; explain any
unsupported features. Let users name groups and split them automatically by actual
limits. Preserve existing SD libraries, attribution and licensing metadata. Imported
faces must participate in Favorites. End users need no Python installation, terminal
commands, programming or firmware flashing. Personal importing is separate from
which artwork may be redistributed in the official release.

These enhancements are separate from the released v0.5.1 installer. Implement and
validate Favorites first.
