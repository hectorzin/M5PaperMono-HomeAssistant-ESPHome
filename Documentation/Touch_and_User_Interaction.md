# Touch and User Interaction

The FT6336G touch panel uses calibration `x=5..475`, `y=5..795` and is polled every 50 ms. Touch reports activity, wakes light sleep, enables the active path, and may turn on the frontlight. BMI270 motion is the other activity source and can wake through M5PM1.

Touching the dashboard requests entry to controls; if Wi-Fi/API/HA are not ready, entry remains pending until they recover. The top back area returns home. GPIO2/GPIO3 navigate controls pages, subject to the current multi-block/at-most-six-controls limitation described in [Rooms and Controls](Rooms_and_Controls.md). A single POWER-button click also returns Home while Controls or pending Controls is active. A short buzzer feedback pulse is generated for supported state-changing interactions; the buzzer is GPIO42 and the pulse is 25 ms at 8% output.

Only touch and motion reset inactivity timing. Physical page/POWER buttons, Home Assistant state updates, display refreshes, NFC/status LED pulses, and the buzzer pulse do not. The controls view is closed before light sleep. User activity during periodic recovery cancels the recovery.

The frontlight is normally off, turns on at the runtime brightness setting after activity, and turns off when the frontlight timeout measured from the last touch/motion activity expires. The runtime brightness number changes the persistent default; it does not by itself reapply the level to an already-lit frontlight. Directly controlling the `Frontlight` entity applies its requested level, but does not reset the touch/motion inactivity clock. The device may remain awake with Wi-Fi/API active until the separate sleep timeout expires.
