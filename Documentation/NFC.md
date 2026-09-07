# NFC

## Hardware

The ST25R3916 uses the main I²C bus:

| Signal | Value |
|---|---|
| I²C address | `0x50` |
| SDA / SCL | GPIO47 / GPIO48 |
| I²C speed | 100 kHz in this firmware |
| NFC power | M5IOE1 GPIO4, active high |
| NFC IRQ | ESP32 GPIO6, rising edge |

NFC is powered during normal operation. It is stopped and powered off before ESP32 light sleep and remains off during sleep. A periodic TIMER wake only restores Wi-Fi/API for the refresh, so NFC remains off throughout that recovery and the following return to light sleep. Touch/motion user wake powers NFC and initializes it again. PMIC shutdown also removes NFC power.

## Reading and low-power detection

The reader implements ISO14443A polling through REQA and the anti-collision/select sequence. BCC and UID cascade responses are validated. The resulting UID is uppercase hexadecimal without separators. The current NFC-A driver walks up to three cascade levels, covering 4-byte, 7-byte, and 10-byte UIDs.

The text sensor `Última tarjeta NFC` (`nfc_last_uid`) is published only after UID confirmation. A present card is not continuously republished: the reader waits for removal, checks amplitude every 100 ms, and rearms after three stable removal samples.

At startup the reader calibrates an amplitude reference and arms wake detection with delta 3 and a 100 ms internal timer. An amplitude IRQ starts active-reader confirmation, not an unconditional card acceptance. Up to five confirmation retries occur at 40 ms intervals.

## Navigation

```yaml
controls:
  blocks:
    - name: "Garden"
      nfc_id: "044B2822BE7C80"
      entities:
        - entity_id: light.garden
          name: Garden lights
```

Configured UIDs are normalized with `strip().upper()`, and detected UIDs are generated as uppercase hexadecimal without separators. Letter case and surrounding whitespace therefore do not matter, but separators in a configured UID are not removed. A known tag publishes its UID and opens the block's first page. An unknown tag only changes the UID sensor.

See the [PaperMono NFC UID Reading Guide](PaperMono_NFC_UID_Reading_Guide_EN.md) for the UserDemo validation register sequence and the detailed REQA/CL1/CL2 transaction. Those register values are not an exact specification of the current firmware initialization.
