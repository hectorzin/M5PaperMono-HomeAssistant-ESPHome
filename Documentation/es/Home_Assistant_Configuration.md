# Configuración de Home Assistant

[English](../Home_Assistant_Configuration.md) | [Español](Home_Assistant_Configuration.md)

## Configuración en tiempo de compilación

Edita el YAML del dispositivo creado en ESPHome Device Builder para los valores sustituidos en el firmware generado (el ejemplo del repositorio es `paper_mono.yaml`):

Establece `language: "en"` para inglés (predeterminado) o `language: "es"` para español. Los nombres de entidad públicos de PaperMono siguen esta selección. Como ESPHome deriva actualmente los IDs de entidad de esos nombres visibles, cambiar el idioma puede cambiar los `entity_id` correspondientes al instalar el firmware nuevo. Los IDs internos del firmware, los `entity_id` de Home Assistant configurados por el usuario y los nombres proporcionados en `controls:` no cambian.

| Sustitución | Predeterminado | Propósito |
|---|---:|---|
| `device_name` | `paper-mono` | Nombre del nodo ESPHome |
| `frontlight_default_brightness` | `30` | Porcentaje inicial de luz frontal |
| `frontlight_timeout_seconds` | `30` | Timeout inicial de inactividad de la luz frontal |
| `sleep_timeout_seconds` | `60` | Timeout inicial antes de la transición de energía |
| `screensaver_refresh_minutes` | `5` | Intervalo inicial de actualización periódica en light sleep |
| `quiet_hours_start` / `quiet_hours_end` | `00:00` / `08:00` | Ventana inicial de horas silenciosas |
| `status_led_low_battery_threshold` | `20` | Umbral de batería baja en porcentaje |

Las sustituciones `ha_*_entity` identifican las fuentes del panel. No crean esas entidades.

## Entidades de configuración en tiempo de ejecución

| Entidad | ID | Tipo y rango/opciones | Predeterminado | Persistencia y efecto |
|---|---|---|---:|---|
| `Paper Mono Frontlight Brightness` | `paper_mono_frontlight_brightness` | number, 0–100 `%`, step 1, slider, optimistic | 30 | Restaurado desde NVS; brillo predeterminado persistente para la siguiente aplicación, no actualización inmediata de una luz frontal ya encendida |
| `Paper Mono Frontlight Timeout` | `paper_mono_frontlight_timeout` | number, 1–3600 `s`, step 1, box, optimistic | 30 | Restaurado desde NVS; timeout de apagado medido desde la última actividad táctil/de movimiento |
| `Paper Mono Sleep Timeout` | `paper_mono_sleep_timeout` | number, 10–3600 `s`, step 10, box, optimistic | 60 | Restaurado desde NVS; solicita la ruta de light sleep por inactividad |
| `Paper Mono Refresh Interval` | `paper_mono_refresh_interval` | optimistic select: `1,2,3,4,5,6,10,12,15,20,30,60` minutos | 5 | Restaurado desde NVS; intervalo alineado de wake periódico en light sleep |
| `Paper Mono Quiet Hours Start` | `paper_mono_quiet_hours_start` | texto optimistic, válido `HH:MM` | `00:00` | Restaurado desde NVS; inicio de la ventana de apagado |
| `Paper Mono Quiet Hours End` | `paper_mono_quiet_hours_end` | texto optimistic, válido `HH:MM` | `08:00` | Restaurado desde NVS; objetivo de wake del RTC |
| `Paper Mono Restore Defaults` | `paper_mono_restore_defaults` | button | — | Reaplica los seis valores predeterminados compilados representados por los ajustes persistentes anteriores |

Las seis sustituciones YAML persistentes son valores iniciales solo cuando no existe un valor guardado, o después de restaurar estos valores predeterminados de ejecución. El callback rechaza texto de horas silenciosas no válido. Establecer valores idénticos de inicio y fin desactiva la ventana de horas silenciosas.

## Otras entidades de PaperMono

| Entidad | ID | Tipo | Comportamiento |
|---|---|---|---|
| `Frontlight` | `paper_mono_frontlight` | light | Aplica encendido/apagado y brillo a la salida de luz frontal del M5PM1; un comando de brillo también se convierte en el valor predeterminado persistente |
| `Paper Mono Battery Voltage` | `paper_mono_battery_voltage` | sensor | Entidad de voltaje bruto de batería proporcionada por M5PM1 |
| `Paper Mono Battery Level` | `paper_mono_battery_level` | sensor | Porcentaje bruto de batería proporcionado por M5PM1 |
| `Paper Mono External Power` | `paper_mono_external_power` | binary sensor | Indica la presencia de alimentación externa/USB |
| `Última tarjeta NFC` | `nfc_last_uid` | text sensor | Último UID NFC confirmado, hexadecimal en mayúsculas sin separadores |

Las tarjetas de control de habitaciones no crean entidades de Home Assistant. Se suscriben a los IDs existentes declarados bajo `controls.blocks` y ejecutan acciones de Home Assistant sobre esas entidades.

Las entidades de las dos tablas anteriores son visibles para el usuario, no `internal`. Las suscripciones de fuentes del panel en `packages/home_assistant.yaml` y las suscripciones generadas del estado de controles son entidades internas de implementación.

## Datos y disponibilidad del panel

`packages/home_assistant.yaml` se suscribe a las entidades configuradas de clima, entorno interior, calidad del aire, flujo de energía y batería. Sus últimos valores se almacenan en RAM para el renderizado. Esa RAM sobrevive al light sleep, pero no es persistencia NVS y no está disponible después de un arranque en frío o un apagado del PMIC hasta que HA envía un estado nuevo. Home Assistant también es la autoridad del reloj.

El fallback global `DEMO` solo se activa después de que la API haya estado ausente durante 60 segundos. Durante un wake TIMER periódico, el dispositivo espera hasta 30.000 ms a Wi-Fi/API y después usa la hora local y los valores de HA almacenados en RAM si el intento de recuperación expira. Un periodo de estabilización de 1.750 ms precede a la actualización en ambos casos, después el dispositivo vuelve a dormir. Las acciones de Controls quedan bloqueadas hasta que la API está lista.

También se exponen acciones de la API nativa:

- `sleep_now`: solicita la ruta unificada de sleep. Si la hora actual está dentro de las horas silenciosas, el dispositivo usa la ruta de apagado del PMIC; en otro caso entra en light sleep. `wake_at` puede estar vacío; vacío usa el temporizador de actualización alineado normal, mientras que un `HH:MM` válido programa esa hora del día para light sleep.
- `shutdown_until`: fuerza la arquitectura de apagado del M5PM1 y requiere un `wake_at` válido en formato `HH:MM`.
