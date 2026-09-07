# Getting Started

## Requirements

- M5Stack PaperMono C153
- ESPHome 2026.6.x or later
- Local Wi-Fi and Home Assistant with the ESPHome integration
- Several gigabytes of free storage; the first ESP-IDF/PlatformIO build may require about 10 GB

The project targets an ESP32-S3 with 16 MB flash and octal PSRAM. The custom e-paper component is fixed at 800×480.

## Project layout

`paper_mono.yaml` is the only firmware YAML intended for normal user editing. It defines substitutions, Home Assistant entity IDs, control blocks, and package includes.

| Path | Responsibility |
|---|---|
| `packages/` | Connectivity, hardware, dashboard, controls, power, battery, LEDs, and runtime settings |
| `components/` | Custom ESPHome components and drivers |
| `fonts/` | Display fonts |
| `Documentation/` | User and developer documentation |
| `secrets.yaml` | Local Wi-Fi credentials; do not commit it |

## Secrets and configuration

```powershell
Copy-Item secrets.example.yaml secrets.yaml
```

Set the Wi-Fi credentials, then edit the substitutions in `paper_mono.yaml`, especially `device_name` and the `ha_*_entity` values. HEAD uses DHCP. If a deployment requires a fixed address, add an ESPHome `manual_ip` configuration as a local network customization without assuming any repository-wide address.

## Validate, compile, and install

```text
esphome config paper_mono.yaml
esphome compile paper_mono.yaml
esphome run paper_mono.yaml
```

`run` installs the firmware using the selected ESPHome transport. OTA is enabled after the device is reachable. Home Assistant discovers the device through the ESPHome native API.

The device exposes its frontlight, battery voltage and level, external-power state, NFC last UID, and runtime configuration entities. Control cards do not create additional Home Assistant entities: they consume and operate the existing entities configured in `paper_mono.yaml`. See [Home Assistant Configuration](Home_Assistant_Configuration.md).

If Wi-Fi credentials cannot be used, the configured fallback AP is `${device_name}-setup`, protected by the Wi-Fi secret password. On Windows, non-ASCII project paths can cause font-loading errors; use an ASCII path if that occurs.
