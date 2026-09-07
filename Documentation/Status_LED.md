# Status LED

The status hardware has a red M5PM1 LED and green/blue M5IOE1 channels.

| State | LED | Pattern | Meaning |
|---|---|---|---|
| Normal, battery valid | Off | Off | No alert and no external power |
| External power, battery below 100% or unavailable | Red | Steady | External/USB power |
| External power, battery 100% | Green | Steady | External power and full battery |
| Battery below threshold | Red | Approximately 40 ms, at most about once per second | Low battery; active policy mode 4 |
| HA unavailable after the 60 s decision | Blue | 40 ms pulse, at most once per second | API/HA alert |
| Low battery and HA unavailable | Red/blue | Alternating 40 ms pulses, at most about once per second | Both alerts |
| Color preview | RGB channels | Preview-controlled | Preview owns channels temporarily |

The low-battery threshold is `< 20%` by default. External power has priority over low-battery and HA alert modes. HA alert is based on confirmed `DEMO`, not transient connecting state. Periodic wake may generate the same short alert pulse.

LED work does not report activity or create a wake source. Normal alert pulses are suppressed while sleep is pending, during TIMER recovery, and while a color preview owns the channels. The separate `status_led_periodic_pulse` is intentionally allowed during TIMER recovery even though sleep remains pending; activity waits for its approximately 40 ms pulse to finish before returning to light sleep. Status updates do not run while the ESP32 is sleeping, and preview state is not persisted across reboot.
