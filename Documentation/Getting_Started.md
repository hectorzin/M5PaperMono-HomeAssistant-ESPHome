# Getting Started

[English](Getting_Started.md) | [Español](es/Getting_Started.md)

This is the recommended installation path for Home Assistant users. It uses ESPHome Device Builder and the shared package published on GitHub; cloning this repository or copying `packages/`, `components/`, `custom_components/`, or `external_components/` is not required.

The default language is `language: "en"`. Set `language: "es"` to select Spanish. Changing the language requires recompiling and installing the firmware. ESPHome currently derives public entity IDs from visible `name:` values, so translated names can produce different `entity_id` values after changing language. Firmware-internal IDs remain stable in English, and user-provided Home Assistant `entity_id` values and `controls:` names are never translated.

## Requirements

- M5Stack PaperMono C153
- ESPHome Device Builder in Home Assistant
- Local Wi-Fi and Home Assistant with the ESPHome integration
- Several gigabytes of free storage; the first ESP-IDF/PlatformIO build may require about 10 GB

The project targets an ESP32-S3 with 16 MB flash and octal PSRAM. The custom e-paper component is fixed at 800×480.

## Recommended installation in Home Assistant

1. Install or open **ESPHome Device Builder** in Home Assistant.
2. Create a new ESP32 device and open its YAML editor.
3. In the Builder's `secrets.yaml`, add credentials using placeholders, never a real password in shared documentation:

   ```yaml
   wifi_ssid: "TU_WIFI"
   wifi_password: "TU_PASSWORD"
   ```

4. In the device YAML, use those secrets:

   ```yaml
   substitutions:
     wifi_ssid: !secret wifi_ssid
     wifi_password: !secret wifi_password
   ```

5. Add the Home Assistant entity IDs you want to display or control. See [Home Assistant Configuration](Home_Assistant_Configuration.md) and [Rooms and Controls](Rooms_and_Controls.md).
6. Optionally configure the control blocks and NFC IDs described in [NFC](NFC.md).
7. Keep the remote package block in the YAML:

   ```yaml
   packages:
     paper_mono:
       url: https://github.com/hectorzin/M5PaperMono-HomeAssistant-ESPHome
       ref: main
       files:
         - packages/paper_mono_base.yaml
       refresh: 0s
   ```

   The relative includes inside `paper_mono_base.yaml` resolve within the remote repository checkout. `refresh: 0s` lets builds check the current `main` version instead of keeping an indefinitely stale package copy.

8. Save the YAML and validate it in ESPHome Builder.
9. Perform the first installation over USB if the device is not yet reachable over Wi-Fi.
10. After the device is online, install later updates over OTA.

The device exposes its frontlight, battery voltage and level, external-power state, NFC last UID, and runtime configuration entities. Control cards do not create additional Home Assistant entities: they consume and operate the existing entities configured in the device YAML. See [Home Assistant Configuration](Home_Assistant_Configuration.md).

## Updating the firmware

Because the YAML consumes `main` with `refresh: 0s`, compile and install again from ESPHome Builder to obtain a newly published project version. You do not need to copy the packages or components again. OTA can be used once the device is reachable.

## Development notes

The repository layout, local `components/` tree, and CLI commands are intended for firmware development and troubleshooting, not for the normal Home Assistant installation. See [Architecture](Architecture.md) when working on the implementation itself.

If Wi-Fi credentials cannot be used, the configured fallback AP is `${device_name}-setup`, protected by the Wi-Fi secret password. On Windows, non-ASCII project paths can cause font-loading errors; use an ASCII path if that occurs.
