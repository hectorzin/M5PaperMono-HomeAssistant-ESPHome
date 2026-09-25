# M5PaperMono-HomeAssistant

[English](README.md) | [Español](README_ES.md)

Firmware ESPHome para M5Stack PaperMono C153. Combina la pantalla de tinta electrónica SSD1677 de 800×480, el panel táctil FT6336G, el lector NFC ST25R3916, el sensor de movimiento BMI270, el circuito de gestión de energía M5PM1, la luz frontal y la API nativa de Home Assistant.

El dispositivo muestra un panel de clima y energía, abre páginas de control específicas por habitación y puede manejar luces, interruptores, persianas, dispositivos de climatización, aspiradoras y reproductores multimedia configurados en `paper_mono.yaml`. El táctil, el movimiento, las tarjetas NFC, las actualizaciones periódicas y la gestión de energía durante las horas silenciosas forman parte del funcionamiento normal.

Para una instalación normal, crea un YAML de dispositivo en ESPHome Device Builder de Home Assistant y consume directamente desde GitHub el paquete de firmware publicado. No necesitas clonar este repositorio ni copiar sus directorios `packages/` o `components/`. Los archivos del repositorio sirven como referencia y para desarrollo.

## Hardware

- M5Stack PaperMono C153
- Panel monocromo SSD1677 de tinta electrónica de 800×480
- Controlador táctil FT6336G
- Lector NFC-A ST25R3916
- IMU BMI270
- PMIC M5PM1 y RTC RX8130
- Expansor de E/S M5IOE1

El dispositivo requiere Wi-Fi y utiliza la API nativa de ESPHome para intercambiar con Home Assistant la hora, los valores del panel, el estado de los controles y las acciones.

## Documentación

- [Primeros pasos](Documentation/es/Getting_Started.md)
- [Configuración de Home Assistant](Documentation/es/Home_Assistant_Configuration.md)
- [Habitaciones y controles](Documentation/es/Rooms_and_Controls.md)
- [NFC](Documentation/es/NFC.md)
- [Gestión de energía](Documentation/es/Power_Management.md)
- [Pantalla y actualizaciones](Documentation/es/Display_and_Refresh.md)
- [LED de estado](Documentation/es/Status_LED.md)
- [Táctil e interacción del usuario](Documentation/es/Touch_and_User_Interaction.md)
- [Solución de problemas](Documentation/es/Troubleshooting.md)
- [Arquitectura](Documentation/es/Architecture.md)
- [Guía de lectura de UID del ST25R3916](Documentation/es/PaperMono_NFC_UID_Reading_Guide_ES.md)

La guía de UID registra la secuencia de registros de bajo nivel de UserDemo y las transacciones NFC-A; sus valores de registro no son necesariamente idénticos a la inicialización del firmware actual. La [guía de NFC](Documentation/es/NFC.md) explica cómo se integra ese lector en este firmware.

El idioma de esta documentación es independiente del idioma del firmware. En el YAML de ESPHome, `language: "en"` selecciona el firmware en inglés y `language: "es"` selecciona el firmware en español. Cambiarlo requiere recompilar e instalar nuevamente el firmware.
