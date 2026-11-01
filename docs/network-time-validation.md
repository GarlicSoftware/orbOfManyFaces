# Wi-Fi and time-zone setup validation

Development firmware 0.7.0 adds setup without requiring stock firmware. The
published v0.5.1 installer ZIP is unchanged until a new release is published.

## Automated checks

`tools/test_network_setup.py` compiles the actual `src/network_setup.cpp` against
controlled Arduino, Wi-Fi, Preferences, HTTP and DNS doubles. It exercises:

- Starting/stopping the password-protected setup session and binding HTTP to
  the setup interface, with AP/DNS failures cleaned up.
- Form tokens, invalid credentials, explicit open-network confirmation, invalid
  time zones, HTML escaping and passwords absent from returned page content.
- Connection testing before persistence; wrong-password timeout, cancellation,
  failed NVS writes and corrupted readback restoring previous saved credentials.
- Restart loading, corrupt-record fallback, no-credentials detection, setup
  expiry after ten minutes and explicit Finish.
- Time-zone save errors and independent time-only changes.
- All regional POSIX rules, strict custom-rule validation, record CRC/length
  checks, malformed input and 10,000 randomized parser inputs.

On Linux the same test also checks all four continental US regional presets
across winter/summer, spring-forward and fall-back. Windows host C runtime does
not implement these POSIX rules; the actual ESP32 provides the hardware check.
Existing rotation, input/hourly-chime and Favorites storage tests pass, as do
all 21 installer safety/recovery tests. Public and personal firmware builds pass.

## Device checks

The personal image was flashed only into the application region, with esptool
hash verification. Existing stock Wi-Fi connected without opening setup
automatically. The selected clock/group, correct Eastern rule, 16 Favorites and
the original library catalog were retained.

Read-only `time at` diagnostics evaluated these UTC instants on the ESP32's own
time library without changing its RTC or system time:

| UTC instant | Eastern local result | DST |
|---|---|---|
| 2026-03-08 06:59:59 | 2026-03-08 01:59:59 | No |
| 2026-03-08 07:00:00 | 2026-03-08 03:00:00 | Yes |
| 2026-11-01 05:59:59 | 2026-11-01 01:59:59 | Yes |
| 2026-11-01 06:00:00 | 2026-11-01 01:00:00 | No |

The on-device Time zone menu saved UTC and then restored the original Eastern
rule. Setup opened a hotspot while retaining the existing station connection;
the display with its network/password/address instructions was captured and
visually inspected. The owner confirmed joining the AP and loading the page on
a phone, then entering their current network and receiving **Wi-Fi connected
and saved**. After closing setup and rebooting, saved Wi-Fi reconnected and the
hotspot stayed off. Clock/group, 16 Favorites, libraries, volume, timezone and
timer settings matched the pre-test snapshot. Steady rendering remained about
one frame per second with no group SD reads. The owner's network password was
entered only on the phone page, never in this chat or diagnostic output.

## Why fixed offsets are preserved

Stock auto-location can store a fixed offset without seasonal transitions.
That is a plausible cause of a one-hour seasonal error, but the affected user's
region and diagnostics are not yet known. A fixed offset cannot identify a
unique region, so the firmware offers regional selection rather than silently
changing inherited settings. Users in non-DST regions retain valid fixed rules.

Presets use specific regional labels rather than a generic US/Canada Pacific
choice. Vancouver's current year-round UTC-7 rule shares the non-seasonal
Arizona/Yukon entry, while Los Angeles retains seasonal US Pacific rules. This
was checked against the official
[British Columbia time-zone update](https://www2.gov.bc.ca/gov/content/governments/celebrating-british-columbia/daylight-saving-time).

Regional POSIX examples and runtime handling were checked against the official
[Arduino-ESP32 time example](https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Time/SimpleTime/SimpleTime.ino)
and [Espressif system-time documentation](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/api-reference/system/system_time.html).
