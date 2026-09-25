# Arquitectura

[English](../Architecture.md) | [Español](Architecture.md)

El YAML público del usuario proporciona sustituciones y consume `packages/paper_mono_base.yaml` desde el repositorio de GitHub. Los includes relativos de ese paquete se resuelven dentro del checkout remoto. En el checkout de desarrollo de este repositorio, el mismo paquete puede inspeccionarse localmente y la generación de código de ESPHome lee los esquemas locales de `components/`.

La estructura local y las referencias de código siguientes son para desarrollo y solución de problemas. Un usuario normal de Home Assistant no necesita clonar ni copiar manualmente esos archivos.

| Componente | Responsabilidad |
|---|---|
| `papermono_epaper` | Controlador SSD1677, política FULL/PARTIAL, gestión de BUSY, baseline y recuperación del PMIC |
| `papermono_activity` | Actividad, luz frontal, temporizadores, light sleep, wake periódico y horas silenciosas |
| `papermono_nfc` | Alimentación del ST25R3916, wake por amplitud, confirmación de UID NFC-A, retirada y navegación |
| `controls` | Dominios generados, mapeo de bloques/páginas, estado almacenado y funciones |
| `m5pm1` | Batería/energía, encaminamiento de IRQ, PWM de la luz frontal, apagado y recuperación de arranque |
| `papermono_rtc` | Acceso al RTC RX8130 para programar las horas silenciosas |
| `papermono_imu` | Detección de movimiento BMI270 y ruta de wake |
| `m5ioe1` | Pines del expansor de E/S del panel, táctil, alimentación NFC y LED |

## Flujo de estado y datos

`papermono_activity` posee las transiciones de energía de alto nivel. Un temporizador periódico se alinea con el siguiente límite del intervalo de actualización cuando la hora de HA es válida. El wake TIMER mantiene NFC apagado, espera hasta 30.000 ms a Wi-Fi/API, aplica un periodo de estabilización de 1.750 ms, actualiza con el estado de HA en vivo o almacenado en RAM y vuelve a light sleep. El wake por táctil/movimiento reanuda la ruta activa, incluido NFC. El wake del PMIC es un nuevo arranque de ESP32 seguido de recuperación de hardware, un FULL inicial obligatorio, recuperación de conexión/datos y un FULL final obligatorio.

La pantalla acepta solicitudes del arranque, la UI, callbacks de HA, wake periódico y recuperación del PMIC. Las solicitudes se combinan mientras BUSY; FULL obligatorio prevalece sobre las actualizaciones normales, y las solicitudes automáticas y de usuario combinadas conservan la política automática. PARTIAL solo se permite después de un baseline válido. NFC usa normalmente detección de amplitud, confirma un UID mediante sondeo NFC-A, lo publica, lo asigna a una página de bloque y espera la retirada antes de rearmarse.

Los sensores de Home Assistant almacenan en RAM los valores del panel y de los controles. La caché sobrevive a light sleep, pero no a un arranque en frío ni al apagado del PMIC. Las suscripciones de controles generadas se crean a partir del dominio y los atributos de cada entidad; no crean nuevas entidades de control de Home Assistant. Las acciones táctiles solo llaman a servicios de HA cuando la API está lista, y una entrada solicitada en Controls permanece pendiente hasta que se cumple esa condición. El renderizado puede continuar con valores almacenados durante el fallback periódico.

Referencias importantes: `components/controls/__init__.py`, `components/papermono_activity/papermono_activity.cpp`, `components/papermono_epaper/papermono_epaper.cpp`, `components/papermono_nfc/papermono_nfc.cpp`, `components/m5pm1/m5pm1.cpp`, `packages/runtime_config.yaml` y `packages/home_assistant_controls.yaml`.
