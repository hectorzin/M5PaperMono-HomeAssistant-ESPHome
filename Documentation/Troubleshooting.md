# Troubleshooting

## No Home Assistant connection

Check `wifi_ssid`, `wifi_password`, DHCP/network reachability, and ESPHome logs. HEAD uses DHCP; a fixed address exists only if the user adds an optional `manual_ip` customization. The native API state is authoritative for controls and alerts. RAM-cached data may still render after light sleep, but actions are blocked until the API is ready.

## Wi-Fi does not recover after sleep

Look for `Enabling WiFi after light sleep`, `Periodic wake: API ready`, or `Periodic wake: network/API timeout`. TIMER wake waits up to 30,000 ms for Wi-Fi/API and then uses a 1,750 ms settle period. After timeout the current firmware refreshes using local time and HA data retained in RAM from before light sleep, then returns to light sleep. NFC intentionally remains off throughout this periodic recovery.

## Display is stale or does not change

Inspect `EPD physical refresh request`, `PARTIAL refresh`, `FULL refresh`, and `BUSY timeout`. Requests can be coalesced while the panel is busy. After PMIC power loss, the first valid update must be a mandatory FULL.

## E-paper BUSY timeout

The timeout is 15 seconds. Check e-paper power, reset, BUSY wiring, and M5IOE1 availability. A timeout invalidates the partial baseline and blocks subsequent ordinary refreshes. There is no general automatic recovery loop for this state; a reboot or the explicit PMIC hardware-recovery path may be required.

## NFC does not detect a tag

Check `NFC power ON`, `NFC IRQ enabled`, and `NFC low-power detection armed`. Confirm address `0x50`, SDA/SCL GPIO47/GPIO48, M5IOE1 GPIO4, and IRQ GPIO6. NFC is intentionally off during light sleep, TIMER periodic recovery, and PMIC shutdown; it is reinitialized after touch/motion user wake.

## UID is not recognized

Use hexadecimal without separators in `nfc_id`. Uppercase is not required because configuration applies `strip().upper()`, but separators are not removed. A confirmed unknown tag still updates `Última tarjeta NFC` but does not navigate. Compare the UID log with the block configuration.

## Device does not sleep or wake

Sleep waits for an idle display and no pending refresh. A queued refresh, recovery, user activity, or active quiet-hours user override can delay it. In light sleep, test touch and motion separately. A `SLEEP_TIMEOUT` or `sleep_now` request remains a light-sleep path even if the clock is inside quiet hours. During actual PMIC shutdown, touch cannot wake the device; use RTC, motion, or POWER.

## Periodic wake has no HA

This is supported fallback behavior. The log `refreshing with local time and cached HA state` means the device reached the 30,000 ms timeout, will use RAM-cached state after the 1,750 ms settle delay, refresh, and sleep again. Diagnose persistent failures as Wi-Fi/API issues.

## Quiet hours and battery

The normal quiet-hours scheduler uses PMIC shutdown, but `SLEEP_TIMEOUT` and `sleep_now` deliberately remain light-sleep paths. Only actual PMIC shutdown guarantees that periodic e-paper wake does not run. Equal start/end values disable quiet hours. Check the two `Paper Mono Quiet Hours ...` entities and the clock. With USB/5VIN present, PMIC shutdown may immediately repower the ESP32. For battery, check `Paper Mono Battery Voltage`, `Paper Mono Battery Level`, and `Paper Mono External Power`. Battery presentation uses a three-sample moving average; low battery is below 20% by default and produces an approximately 40 ms red pulse at most about once per second.

## ESPHome compilation

Run `esphome config paper_mono.yaml` first. Confirm `secrets.yaml`, font readability, and a compatible ESPHome version. The first ESP-IDF/PlatformIO download is large. On Windows, use an ASCII project path if font loading fails.
