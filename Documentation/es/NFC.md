# NFC

[English](../NFC.md) | [Español](NFC.md)

## Hardware

El ST25R3916 utiliza el bus I²C principal:

| Señal | Valor |
|---|---|
| Dirección I²C | `0x50` |
| SDA / SCL | GPIO47 / GPIO48 |
| Velocidad I²C | 100 kHz en este firmware |
| Alimentación NFC | M5IOE1 GPIO4, activa en nivel alto |
| IRQ NFC | ESP32 GPIO6, flanco ascendente |

NFC está alimentado durante el funcionamiento normal. Se detiene y apaga antes de ESP32 light sleep y permanece apagado durante el sleep. Un wake TIMER periódico solo restaura Wi-Fi/API para la actualización, por lo que NFC permanece apagado durante esa recuperación y el retorno posterior a light sleep. El wake de usuario por táctil/movimiento alimenta NFC y lo inicializa de nuevo. El apagado del PMIC también retira la alimentación de NFC.

## Lectura y detección de bajo consumo

El lector implementa sondeo ISO14443A mediante REQA y la secuencia de anticollision/select. Se validan BCC y las respuestas de cascada del UID. El UID resultante es hexadecimal en mayúsculas sin separadores. El controlador NFC-A actual recorre hasta tres niveles de cascada, cubriendo UIDs de 4, 7 y 10 bytes.

El sensor de texto `Última tarjeta NFC` (`nfc_last_uid`) solo se publica después de confirmar el UID. Una tarjeta presente no se vuelve a publicar continuamente: el lector espera la retirada, comprueba la amplitud cada 100 ms y se rearma después de tres muestras estables de retirada.

Al arrancar, el lector calibra una referencia de amplitud y arma la detección de wake con delta 3 y un temporizador interno de 100 ms. Una IRQ de amplitud inicia la confirmación con el lector activo, no una aceptación incondicional de tarjeta. Se realizan hasta cinco reintentos de confirmación a intervalos de 40 ms.

## Navegación

```yaml
controls:
  blocks:
    - name: "Garden"
      nfc_id: "044B2822BE7C80"
      entities:
        - entity_id: light.garden
          name: Garden lights
```

Los UID configurados se normalizan con `strip().upper()`, y los UID detectados se generan como hexadecimal en mayúsculas sin separadores. Por tanto, no importan las mayúsculas/minúsculas ni los espacios exteriores, pero los separadores de un UID configurado no se eliminan. Una tarjeta conocida publica su UID y abre la primera página del bloque. Una tarjeta desconocida solo cambia el sensor de UID.

Consulta la [Guía de lectura de UID NFC de PaperMono](PaperMono_NFC_UID_Reading_Guide_ES.md) para la secuencia de registros de validación de UserDemo y la transacción detallada REQA/CL1/CL2. Esos valores de registro no son una especificación exacta de la inicialización actual del firmware.
