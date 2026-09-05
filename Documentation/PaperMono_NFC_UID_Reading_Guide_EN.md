# PaperMono NFC-A UID Reading Guide

The onboard ST25R3916 in PaperMono is connected as follows:

```text
I2C address: 0x50
I2C SDA: GPIO47
I2C SCL: GPIO48
I2C frequency: 400 kHz
NFC power: M5IOE1 GPIO4, active high
```

The following sequence has been verified:

```text
REQA -> ATQA       Success
93 20 -> UID CL1  Success
93 70 -> SAK       Success
95 20 -> UID CL2  Success
```

## 1. Register and command sequence from REQA to CL1

### NFC-A initialization

The main configuration verified on PaperMono is:

```text
REG 0x00 / 0x01          0x10 / 0x84
REG 0x02                  0x00
REG 0x03                  0x09   ISO14443A initiator + NFC-A automatic
REG 0x04                  0x00   TX/RX 106 kbit/s
REG 0x05                  0x00
REG 0x08                  0x50
REG 0x0A                  0x00
REG 0x0B                  0x08
REG 0x0C                  0x2D
REG 0x0D                  0xD8
REG 0x0E                  0x22
```

Space-B:

```text
Space-B 0x05              0x40
Space-B 0x30              0x40
Space-B 0x31              0x03
Space-B 0x32              0x40
Space-B 0x33              0x03
Space-B 0x0C              0x47
Space-B 0x0D              0x00
```

Initialization Direct Commands:

```text
CMD_STOP_ALL_ACTIVITIES   0xC2
CMD_SET_DEFAULT           0xC1
CMD_TEST_ACCESS           0xFC, parameters 04 10
CMD_RESET_RX_GAIN         0xD5
CMD_ADJUST_REGULATORS     0xD6
CMD_NFC_INITIAL_FIELD_ON  0xC8
```

### REQA -> ATQA

```text
REG 0x10                  0x03
REG 0x11                  0x50       NRT/FWT approximately 4 ms
REG 0x05                  0x01       antcl = 1
REG 0x0A bit7             1          no_crc_rx = 1

Read and clear IRQ
CMD_CLEAR_FIFO            0xDB
CMD_TRANSMIT_REQA         0xC6
```

Wait for `RXE`, then read `REG 0x1E/0x1F` to obtain the FIFO length, and use `0x9F` to read the 2-byte ATQA response.

### CL1: 93 20 -> UID

```text
REG 0x10                  0x06
REG 0x11                  0x9F       NRT/FWT approximately 8 ms
REG 0x05                  0x01       antcl = 1
REG 0x0A bit7             0          no_crc_rx = 0

Read and clear IRQ
CMD_CLEAR_FIFO            0xDB

Write FIFO command         0x80
FIFO data                  93 20

REG 0x22 / 0x23           0x00 0x10  2 bytes / 16 bits
CMD_TRANSMIT_WITHOUT_CRC  0xC5
```

Wait for `RXE` or `COL`, then read:

```text
REG 0x1E / 0x1F           FIFO status
REG 0x20                  COLLISION_DISPLAY
CMD_READ_FIFO             0x9F
```

With a single tag, the normal response is 5 bytes:

```text
UID0 UID1 UID2 UID3 BCC
```

BCC check:

```text
UID0 ^ UID1 ^ UID2 ^ UID3 == BCC
```

## 2. Does the receiver need to be reconfigured when switching from REQA to CL1?

There is no need to rewrite the complete receiver configuration, RX gain, or correlator settings. The following configuration only needs to be applied once during NFC-A initialization:

```text
REG 0x0B                  0x08
REG 0x0C                  0x2D
REG 0x0D                  0xD8
REG 0x0E                  0x22
Space-B 0x0C              0x47
Space-B 0x0D              0x00
```

When switching from REQA to the anti-collision stage, the following settings need to be changed or read:

```text
REG 0x05                  antcl = 1
REG 0x0A                  Set no_crc_rx according to whether the response includes CRC
REG 0x10 / 0x11           Set NRT/FWT for the current stage
REG 0x1A / 0x1B / 0x1C   Read IRQ
REG 0x1E / 0x1F           Read FIFO status
REG 0x20                  Read collision position
```

For the SELECT stage, switch back to the normal NFC-A settings:

```text
REG 0x05                  0x00
REG 0x0A bit7             0
```

Then transmit:

```text
93 70 UID0 UID1 UID2 UID3 BCC
REG 0x22 / 0x23           7 bytes / 56 bits
CMD_TRANSMIT_WITH_CRC     0xC4
```

Read the returned SAK. If `SAK & 0x04` is non-zero, for example `SAK=0x24`, CL2 must also be executed:

```text
95 20 -> CL2 UID
95 70 -> CL2 SELECT
```

A 10-byte UID also requires CL3:

```text
97 20
97 70
```

## 3. Does PaperMono require any additional special configuration?

No additional hidden NFC-A RF configuration specific to PaperMono has been identified beyond the standard `M5Unit-NFC` procedure.

The PaperMono-specific details are mainly related to the hardware connections:

```text
ST25R3916 uses I2C address 0x50
NFC_EN is controlled by M5IOE1 GPIO4
The internal I2C bus uses GPIO47/GPIO48
UserDemo uses 400 kHz I2C
```

The PaperMono UserDemo does not use the dedicated ST25R3916 IRQ pin. Instead, it polls the IRQ status registers over I2C. Therefore, an ESPHome component can also use polling.

With a single tag, the absence of `COL` is normal. The key items to check are `RXE`, FIFO length, error IRQs, and `COLLISION_DISPLAY`.

## 4. Source code paths

### PaperMono raw-I2C validation demo

Relative path in the provided source project:

```text
main/NFC_Demo.cpp
```

Main functions:

```text
nfc_demo_start()       Starts the demo
power_nfc()            Initializes M5IOE1 and powers on NFC
init_st25r3916()       Initializes ST25R3916
request_atqa()         REQA/WUPA -> ATQA
anticollision()        93/95/97 20 -> UID + BCC
select_level()         93/95/97 70 -> SAK
halt_card()            HLTA
read_uid_levels()      Handles UID cascade levels
scan_task()            Polling and display state machine
```

### Original M5Unit-NFC implementation

The following paths refer to the original `M5Unit-NFC` source tree and are provided for reference. These files are not included in the standalone PaperMono demo project attached separately.

```text
components/M5Unit-NFC/src/unit/unit_ST25R3916_nfca.cpp
components/M5Unit-NFC/src/unit/unit_ST25R3916.cpp
components/M5Unit-NFC/src/unit/ST25R3916_definition.hpp
components/M5Unit-NFC/src/nfc/layer/a/nfc_layer_a.cpp
components/M5Unit-NFC/src/nfc/layer/a/nfc_layer_a_ST25R3916.cpp
```

Corresponding functions:

```text
configure_nfc_a()
nfca_request_wakeup()
nfca_anti_collision()
nfcaSelectWithAnticollision()
nfcaSelect()
```
