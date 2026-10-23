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

## 3. Automatic face rotation (planned)

Automatically advance only within the currently selected group, including
Favorites. Never change groups automatically. Continue within a new group when
the user selects it manually. Default to Off so existing behavior is preserved.

Offer intervals of Off, 5 minutes, 15 minutes, 30 minutes, 1 hour, 2 hours,
6 hours, 12 hours and 24 hours. Align changes to local clock boundaries: for
example, 15 minutes means :00, :15, :30 and :45; multi-hour periods align from
local midnight. Provide Sequential mode, wrapping through the group's existing
order, and Random mode, preferably using a shuffle without repeats until all
faces have been displayed. Handle empty and single-face groups safely.

Provide simple interval and mode controls in the Orb interface and persist both
across restarts. Manual swipes continue working without disabling rotation or
resetting its boundary schedule. Reuse existing face switching and the selected
group's RAM cache; automatic changes must not add SD reads, unnecessary redraws,
recurring allocations or significant overhead.

Handle startup, time synchronization, clock adjustments and daylight-saving
changes without immediate catch-up bursts or repeated boundary triggers. Do not
interrupt the group menu, Favorites operations or hourly chime. Define safe
boundary handling while menus/builds are active so missed intervals never cause
rapid rotations when normal clock display resumes.

This is a separate, comparatively small enhancement alongside Favorites and the
importer; adding it to this roadmap does not enable it in current firmware.

## 4. Automatic group rotation (planned)

Allow users to switch clock groups automatically on a configurable timer,
including Favorites groups. Keep this optional and disabled by default, with
settings independent of face rotation. Face rotation alone must continue to stay
within the selected group; only enabling group rotation permits timed changes
between groups.

Provide simple on-device controls for the group interval and participating
libraries, and remember those settings across restarts. Coordinate group and
face timers so a shared boundary produces one predictable transition rather
than two rapid changes. Manual group selection must remain available. Handle
restarts and time adjustments without catch-up bursts, and defer transitions
while menus, Favorites builds or USB transfers are active.

Reuse normal group loading and RAM caching. A group change may read the SD card
to preload its artwork; subsequent ticks and face changes remain cached. Skip
missing or unavailable libraries safely, preserve Favorites and unrelated card
contents, and avoid interfering with the hourly chime. Final interval choices
and group ordering controls can be settled when implementing this feature.

These enhancements are separate from the released v0.5.1 installer. Implement and
validate Favorites first, with the importer next and automatic rotation tracked
independently.
