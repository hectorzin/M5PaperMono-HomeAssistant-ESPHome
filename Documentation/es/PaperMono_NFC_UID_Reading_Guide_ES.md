# Guía de lectura de UID NFC-A de PaperMono

[English](../PaperMono_NFC_UID_Reading_Guide_EN.md) | [Español](PaperMono_NFC_UID_Reading_Guide_ES.md)

El ST25R3916 integrado en PaperMono está conectado así:

```text
I2C address: 0x50
I2C SDA: GPIO47
I2C SCL: GPIO48
I2C frequency: 400 kHz
NFC power: M5IOE1 GPIO4, active high
```

Este documento es la referencia de validación de bajo nivel de PaperMono/UserDemo. Las tablas de registros siguientes describen ese UserDemo y no son una especificación exacta de la inicialización actual del firmware. El firmware ESPHome actual usa el mismo cableado y flujo de transacciones NFC-A, pero su `bus_main` está configurado a 100 kHz en `paper_mono.yaml`. Los detalles de integración del firmware, la máquina de estados de bajo consumo, la publicación del UID y la navegación por bloques están documentados en [NFC.md](NFC.md).

La siguiente secuencia ha sido verificada:

```text
REQA -> ATQA       Success
93 20 -> UID CL1  Success
93 70 -> SAK       Success
95 20 -> UID CL2  Success
```

## 1. Secuencia de registros y comandos de REQA a CL1

### Inicialización NFC-A

La configuración principal verificada en PaperMono es:

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

Comandos directos de inicialización:

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

Espera a `RXE`, después lee `REG 0x1E/0x1F` para obtener la longitud del FIFO y usa `0x9F` para leer la respuesta ATQA de 2 bytes.

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

Espera a `RXE` o `COL` y después lee:

```text
REG 0x1E / 0x1F           FIFO status
REG 0x20                  COLLISION_DISPLAY
CMD_READ_FIFO             0x9F
```

Con una sola tarjeta, la respuesta normal es de 5 bytes:

```text
UID0 UID1 UID2 UID3 BCC
```

Comprobación BCC:

```text
UID0 ^ UID1 ^ UID2 ^ UID3 == BCC
```

## 2. ¿Es necesario reconfigurar el receptor al cambiar de REQA a CL1?

No es necesario reescribir toda la configuración del receptor, la ganancia RX ni los ajustes del correlador. La siguiente configuración solo debe aplicarse una vez durante la inicialización NFC-A:

```text
REG 0x0B                  0x08
REG 0x0C                  0x2D
REG 0x0D                  0xD8
REG 0x0E                  0x22
Space-B 0x0C              0x47
Space-B 0x0D              0x00
```

Al cambiar de REQA a la fase de anticollision hay que cambiar o leer los siguientes ajustes:

```text
REG 0x05                  antcl = 1
REG 0x0A                  Set no_crc_rx according to whether the response includes CRC
REG 0x10 / 0x11           Set NRT/FWT for the current stage
REG 0x1A / 0x1B / 0x1C   Read IRQ
REG 0x1E / 0x1F           Read FIFO status
REG 0x20                  Read collision position
```

Para la fase SELECT, vuelve a los ajustes NFC-A normales:

```text
REG 0x05                  0x00
REG 0x0A bit7             0
```

Después transmite:

```text
93 70 UID0 UID1 UID2 UID3 BCC
REG 0x22 / 0x23           7 bytes / 56 bits
CMD_TRANSMIT_WITH_CRC     0xC4
```

Lee el SAK devuelto. Si `SAK & 0x04` no es cero, por ejemplo `SAK=0x24`, también debe ejecutarse CL2:

```text
95 20 -> CL2 UID
95 70 -> CL2 SELECT
```

Un UID de 10 bytes también requiere CL3:

```text
97 20
97 70
```

## 3. ¿Requiere PaperMono alguna configuración especial adicional?

No se ha identificado ninguna configuración RF NFC-A oculta específica de PaperMono más allá del procedimiento estándar de `M5Unit-NFC`.

Los detalles específicos de PaperMono están principalmente relacionados con las conexiones de hardware:

```text
ST25R3916 uses I2C address 0x50
NFC_EN is controlled by M5IOE1 GPIO4
The internal I2C bus uses GPIO47/GPIO48
UserDemo uses 400 kHz I2C
```

El UserDemo de PaperMono no usa el pin IRQ dedicado del ST25R3916. En su lugar, sondea los registros de estado IRQ por I2C. Por tanto, un componente ESPHome también puede usar sondeo. La integración actual del firmware documentada en [NFC.md](NFC.md) usa además la línea IRQ del ST25R3916 en ESP32 GPIO6 para iniciar la ruta de wake/confirmación de bajo consumo.

Con una sola tarjeta, la ausencia de `COL` es normal. Los elementos clave que hay que comprobar son `RXE`, la longitud del FIFO, las IRQ de error y `COLLISION_DISPLAY`.

## 4. Rutas de código fuente

### Demo de validación raw-I2C de PaperMono

Ruta relativa en el proyecto fuente proporcionado:

```text
main/NFC_Demo.cpp
```

Funciones principales:

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

### Implementación original de M5Unit-NFC

Las siguientes rutas se refieren al árbol de código fuente original de `M5Unit-NFC` y se proporcionan como referencia. Estos archivos no están incluidos en el proyecto standalone de la demo de PaperMono adjunto por separado.

```text
components/M5Unit-NFC/src/unit/unit_ST25R3916_nfca.cpp
components/M5Unit-NFC/src/unit/unit_ST25R3916.cpp
components/M5Unit-NFC/src/unit/ST25R3916_definition.hpp
components/M5Unit-NFC/src/nfc/layer/a/nfc_layer_a.cpp
components/M5Unit-NFC/src/nfc/layer/a/nfc_layer_a_ST25R3916.cpp
```

Funciones correspondientes:

```text
configure_nfc_a()
nfca_request_wakeup()
nfca_anti_collision()
nfcaSelectWithAnticollision()
nfcaSelect()
```
