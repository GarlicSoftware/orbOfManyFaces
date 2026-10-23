# Favorites development validation

Tested 2026-10-22 on the Waveshare ESP32-S3-Touch-AMOLED-1.75 (16 MB flash,
8 MB PSRAM). This is development work separate from the public v0.5.1 release.

## Physical Orb

- Favorites toggles saved immediately without changing the active face/library.
- Four bookmarks and Later survived restart. Further changes caused a fresh prompt;
  Update Favorites remained available after deferral.
- Five selected designs built into a normal .ocg group and rendered correctly,
  including a built-in Meridian design and clock faces from installed SD libraries.
- Stress test: 82 explicitly selected personal clock faces built into eight groups
  in approximately 229 seconds. All 82 rendered and switched successfully. Groups
  held 8, 4, 9, 10, 12, 14, 17 and 8 faces, demonstrating memory-based grouping
  rather than a fixed face-count preference. The .ocg format still has its existing
  hard limit of 32 records per file.
- The requested names appeared in order through We Need to Talk, followed by
  This Is Getting Out of Hand and The Favorites Have Favorites.
- SD group-read counters did not increase while swiping cached Favorites.
- Removing bookmarks retained existing generated groups until rebuilding. After
  restart and rebuild, eight groups consolidated into one Favorites group holding
  exactly the two remaining selected designs. An empty selection removed all active
  Favorites groups after approval.
- Blue Meridian was absent from the 82-face selection. When explicitly selected
  alone, it appeared exactly once in a one-face Favorites group.
- All nine original SD libraries retained their pre-test SHA-256 hashes. Original
  clock selection, timezone and volume were restored, and temporary selections
  were cleared before handing controls back to the owner.
- Normal steady clock rendering remained approximately one frame per second.

The stress test exposed allocator fragmentation during a later face switch after
initial group validation. The builder now retains a conservative working arena
(up to 6 MiB, capped by actual free PSRAM) while measuring packed bytes, decoded
image sizes and font metadata. The renderer also retries from RAM with the previous
face's decoded images released if a simultaneous allocation fails. All 82 face
switches passed after those changes. Buffered artwork writes avoid per-field SD
operations and scheduling delays.

## Automated checks

Follow-up menu adjustment: rebuilt the owner's six saved favorites on the Orb
and verified completion returns to the normal group menu without another hold.
Selections, current clock, volume and timezone were preserved. Both firmware
builds passed; button tests verify the one-second threshold, one-shot hold and
release behavior.

- Canonical serializer round-tripped all 42 public faces through the actual C++
  .ocg parser without changing their canonical representation or memory sizes.
- Partitioning exercised compressed limits, decoded-image peaks, font metadata,
  more than 12 small faces, the format's 32-record bound, and consolidation.
- The actual favorites.cpp ran against filesystem/hash test doubles: restart
  persistence, scoped Later, validation rollback, torn catalog and selection writes,
  missing-card rollback, CRC fallback and damaged-list protection passed.
- The actual renderer ran against a strict CO5300 panel model for 1,000 tick frames.
  Every panel window retained even starts/sizes and matched full-frame delivery.
  Star fade removal restored underlying pixels exactly at 0, 90 and 37 degrees;
  only small rectangles were transmitted. Long selected names scrolled. Injected
  allocation failure recovered through the RAM-only face-switch fallback.
- The 21 v0.5.1 installer safety/recovery tests remained green. GitHub checks for
  the feature code passed on Python 3.10-3.12 on Windows and Linux, with the native
  favorites tests and firmware build also passing on Linux.

Filesystem interruptions and missing cards were simulated for failure testing;
this run did not physically remove the owner's SD card or cut its power mid-write.
The owner's personal artwork and generated Favorites are not bundled in this repo.
