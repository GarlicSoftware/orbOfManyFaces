# Rotation timer validation

Tested on the development branch with the owner's existing Orb, SD libraries
and Favorites. No device time was changed to accelerate the hardware tests.

## Host checks

- Compiled the actual `rotation_logic.h` with MSVC. Simulated a full local day
  for every supported interval, including midnight and duplicate loop samples.
- Verified packed settings round trips, invalid stored interval fallback, Off,
  startup at a boundary, invalid/recovered time, forward/backward corrections,
  synchronization during the repeated DST hour and spring-forward gaps.
- Verified blocked boundaries coalesce into one pending transition; resuming
  never replays a queue. Long gaps and setting changes re-arm safely.
- Exercised 0/1/2/3/10/33/80-item shuffle bags for 50 complete cycles each,
  including manual visits, no immediate cycle-boundary repeat and capacity reuse.
- Existing input/hourly-chime logic tests, Favorites serialization/storage
  tests and all 21 installer safety/recovery tests passed.
- Renderer tests passed 1,000 clock frames with strict panel alignment, exact
  star restoration at 0/90/37 degrees, scrolling labels and allocation recovery.
- Public and personal firmware builds passed. The personal image was flashed
  only to the application region, with esptool verifying its hash.

## Device checks

USB diagnostics exercised the same menu selection handlers used by knob presses:
every interval, both order modes, Back, menu exit and persisted settings after
reboot. An actual framebuffer capture was inspected for the Settings layout.

A real five-minute face boundary passed while displaying Favorites. A manual
face change just before the boundary did not reset the schedule. Keeping the
group menu open deferred the change; closing it caused exactly one transition.
The group remained the same and the SD group-read counter did not increase.

A second real five-minute boundary had both timers enabled. The Orb loaded the
next SD library, displayed its first face and recorded exactly one automatic
group change, with no simultaneous automatic face step. The SD read counter
increased for the preload. No duplicate transition followed.

After testing, both timers were set to Off and the original clock was restored.
The owner's 16 selections, two generated Favorites groups, original library
catalog, volume and timezone matched the saved pre-test state. A final rebuild
changed the Settings heading from CLOCK GROUPS to SETTINGS; the renderer checks
also passed with this heading parameter.

The owner still needs to confirm the physical knob/swipe experience. Hardware
tests do not attempt every hour/day interval; those boundaries are covered by
the host simulation. Shuffle progress is intentionally not persisted, while
interval and mode settings are stored immediately in the existing clock NVS
namespace. No installer or partition layout changes are required.
