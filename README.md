# M5PaperMono-HomeAssistant

ESPHome firmware for the M5Stack PaperMono C153. It combines the 800×480 SSD1677 e-paper display, FT6336G touch panel, ST25R3916 NFC reader, BMI270 motion sensor, M5PM1 power-management IC, frontlight, and Home Assistant native API.

The device presents a weather and energy dashboard, opens room-specific control pages, and can operate lights, switches, covers, climate devices, vacuums, and media players configured in `paper_mono.yaml`. Touch, motion, NFC cards, periodic refreshes, and quiet-hours power management are part of the normal runtime behavior.

The firmware entry point is [`paper_mono.yaml`](paper_mono.yaml). Hardware and behavior are split into reusable YAML packages under [`packages/`](packages/), while custom ESPHome components live under [`components/`](components/).

## Hardware

- M5Stack PaperMono C153
- 800×480 monochrome SSD1677 e-paper panel
- FT6336G touch controller
- ST25R3916 NFC-A reader
- BMI270 IMU
- M5PM1 PMIC and RX8130 RTC
- M5IOE1 I/O expander

The device requires Wi-Fi and uses the ESPHome native API to exchange time, dashboard values, control state, and actions with Home Assistant.

## Documentation

- [Getting Started](Documentation/Getting_Started.md)
- [Home Assistant Configuration](Documentation/Home_Assistant_Configuration.md)
- [Rooms and Controls](Documentation/Rooms_and_Controls.md)
- [NFC](Documentation/NFC.md)
- [Power Management](Documentation/Power_Management.md)
- [Display and Refresh](Documentation/Display_and_Refresh.md)
- [Status LED](Documentation/Status_LED.md)
- [Touch and User Interaction](Documentation/Touch_and_User_Interaction.md)
- [Troubleshooting](Documentation/Troubleshooting.md)
- [Architecture](Documentation/Architecture.md)
- [ST25R3916 UID Reading Guide](Documentation/PaperMono_NFC_UID_Reading_Guide_EN.md)

The UID guide records the low-level UserDemo register sequence and NFC-A transactions; its register values are not necessarily identical to the current firmware initialization. The [NFC guide](Documentation/NFC.md) explains how that reader is integrated into this firmware.
