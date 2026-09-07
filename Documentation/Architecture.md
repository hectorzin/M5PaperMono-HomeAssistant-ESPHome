# Architecture

`paper_mono.yaml` supplies substitutions and package includes. ESPHome code generation reads local schemas under `components/` and wires generated entities and callbacks into C++.

| Component | Responsibility |
|---|---|
| `papermono_epaper` | SSD1677 driver, FULL/PARTIAL policy, BUSY handling, baseline and PMIC recovery gate |
| `papermono_activity` | Activity, frontlight, timers, light sleep, periodic wake, quiet hours |
| `papermono_nfc` | ST25R3916 power, amplitude wake, NFC-A UID confirmation, removal, navigation |
| `controls` | Generated domains, block/page mapping, cached state and features |
| `m5pm1` | Battery/power, IRQ routing, frontlight PWM, shutdown and boot recovery |
| `papermono_rtc` | RX8130 RTC access for quiet-hours scheduling |
| `papermono_imu` | BMI270 motion detection and wake path |
| `m5ioe1` | Panel, touch, NFC power and LED I/O-expander pins |

## State and data flow

`papermono_activity` owns the high-level power transitions. A periodic timer is aligned to the next refresh-interval boundary when HA time is valid. TIMER wake keeps NFC off, waits up to 30,000 ms for Wi-Fi/API, applies a 1,750 ms settle period, refreshes with live or RAM-cached HA state, and returns to light sleep. Touch/motion wake resumes the active path, including NFC. PMIC wake is a new ESP32 boot followed by hardware recovery, an initial mandatory FULL, connection/data recovery, and a final mandatory FULL.

The display accepts requests from boot, UI, HA callbacks, periodic wake, and PMIC recovery. Requests merge while BUSY; mandatory FULL overrides normal refreshes, and mixed automatic/user requests retain the automatic policy. PARTIAL is allowed only after a valid baseline. NFC normally uses amplitude detection, confirms a UID through NFC-A polling, publishes it, maps it to a block page, and waits for removal before rearming.

Home Assistant sensors cache dashboard/control values in RAM. The cache survives light sleep but not a cold boot or PMIC shutdown. Generated control subscriptions are created from each entity domain and attributes; they do not create new Home Assistant control entities. Touch actions call HA services only when the API is ready, and a requested Controls entry remains pending until that condition is met. Rendering can continue from cached values during periodic fallback.

Important source references: `components/controls/__init__.py`, `components/papermono_activity/papermono_activity.cpp`, `components/papermono_epaper/papermono_epaper.cpp`, `components/papermono_nfc/papermono_nfc.cpp`, `components/m5pm1/m5pm1.cpp`, `packages/runtime_config.yaml`, and `packages/home_assistant_controls.yaml`.
