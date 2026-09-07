# Home Assistant Configuration

## Compile-time configuration

Edit `paper_mono.yaml` for values substituted into the generated firmware:

| Substitution | Default | Purpose |
|---|---:|---|
| `device_name` | `paper-mono` | ESPHome node name |
| `frontlight_default_brightness` | `30` | Initial frontlight percentage |
| `frontlight_timeout_seconds` | `30` | Initial frontlight inactivity timeout |
| `sleep_timeout_seconds` | `60` | Initial timeout before power transition |
| `screensaver_refresh_minutes` | `5` | Initial periodic light-sleep refresh interval |
| `quiet_hours_start` / `quiet_hours_end` | `00:00` / `08:00` | Initial quiet-hours window |
| `status_led_low_battery_threshold` | `20` | Low-battery threshold in percent |

The `ha_*_entity` substitutions identify dashboard sources. They do not create those entities.

## Runtime configuration entities

| Entity | ID | Type and range/options | Default | Persistence and effect |
|---|---|---|---:|---|
| `Paper Mono Frontlight Brightness` | `paper_mono_frontlight_brightness` | number, 0–100 `%`, step 1, slider, optimistic | 30 | Restored from NVS; persistent/default brightness for the next application, not an immediate update of an already-lit frontlight |
| `Paper Mono Frontlight Timeout` | `paper_mono_frontlight_timeout` | number, 1–3600 `s`, step 1, box, optimistic | 30 | Restored from NVS; frontlight-off timeout measured from the last touch/motion activity |
| `Paper Mono Sleep Timeout` | `paper_mono_sleep_timeout` | number, 10–3600 `s`, step 10, box, optimistic | 60 | Restored from NVS; requests the inactivity light-sleep path |
| `Paper Mono Refresh Interval` | `paper_mono_refresh_interval` | optimistic select: `1,2,3,4,5,6,10,12,15,20,30,60` minutes | 5 | Restored from NVS; aligned light-sleep periodic wake interval |
| `Paper Mono Quiet Hours Start` | `paper_mono_quiet_hours_start` | optimistic text, valid `HH:MM` | `00:00` | Restored from NVS; shutdown window start |
| `Paper Mono Quiet Hours End` | `paper_mono_quiet_hours_end` | optimistic text, valid `HH:MM` | `08:00` | Restored from NVS; RTC wake target |
| `Paper Mono Restore Defaults` | `paper_mono_restore_defaults` | button | — | Reapplies the six compiled defaults represented by the persistent runtime settings above |

The six persistent YAML substitutions are initial values only when no saved value exists, or after restoring these runtime defaults. Invalid quiet-hours text is rejected by the callback. Setting identical quiet-hours start and end values disables the quiet-hours window.

## Other PaperMono entities

| Entity | ID | Type | Behavior |
|---|---|---|---|
| `Frontlight` | `paper_mono_frontlight` | light | Applies on/off and brightness to the M5PM1 frontlight output; a brightness command also becomes the persistent default |
| `Paper Mono Battery Voltage` | `paper_mono_battery_voltage` | sensor | Raw battery-voltage entity supplied by M5PM1 |
| `Paper Mono Battery Level` | `paper_mono_battery_level` | sensor | Raw battery percentage supplied by M5PM1 |
| `Paper Mono External Power` | `paper_mono_external_power` | binary sensor | Reports external/USB power presence |
| `Última tarjeta NFC` | `nfc_last_uid` | text sensor | Last confirmed NFC UID, uppercase hexadecimal without separators |

The room-control cards do not create Home Assistant entities. They subscribe to the existing entity IDs declared under `controls.blocks` and invoke Home Assistant actions against those entities.

The entities in the two tables above are user-facing, not `internal`. The dashboard source subscriptions in `packages/home_assistant.yaml` and the generated control-state subscriptions are internal implementation entities.

## Dashboard data and availability

`packages/home_assistant.yaml` subscribes to the configured weather, indoor environment, air-quality, power-flow, and battery entities. Their latest values are cached in RAM for rendering. That RAM survives ESP32 light sleep, but it is not NVS persistence and is unavailable after a cold boot or PMIC shutdown until HA sends fresh state. Home Assistant is also the clock authority.

The global `DEMO` fallback is entered only after the API has been absent for 60 seconds. During a periodic TIMER wake, the device waits up to 30,000 ms for Wi-Fi/API and then uses local time plus the RAM-cached HA values if recovery times out. A 1,750 ms settle period precedes the refresh in either case, after which the device returns to sleep. Controls actions are blocked until the API is ready.

Native API actions are also exposed:

- `sleep_now`: requests ESP32 light sleep and bypasses redirection to PMIC shutdown merely because quiet hours are active. `wake_at` may be empty; an empty value uses the normal aligned refresh timer, while a valid `HH:MM` schedules that time of day. An active `quiet_hours_user_override` can still defer entry into light sleep.
- `shutdown_until`: requests M5PM1 shutdown and requires a valid `wake_at` in `HH:MM` format.
