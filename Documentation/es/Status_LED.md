# LED de estado

[English](../Status_LED.md) | [Español](Status_LED.md)

El hardware de estado tiene un LED rojo M5PM1 y canales verde/azul M5IOE1.

| Estado | LED | Patrón | Significado |
|---|---|---|---|
| Normal, batería válida | Apagado | Apagado | Sin alerta ni alimentación externa |
| Alimentación externa, batería por debajo del 100% o no disponible | Rojo | Fijo | Alimentación externa/USB |
| Alimentación externa, batería al 100% | Verde | Fijo | Alimentación externa y batería completa |
| Batería por debajo del umbral | Rojo | Aproximadamente 40 ms, como máximo una vez por segundo | Batería baja; modo de política activo 4 |
| HA no disponible tras la decisión de 60 s | Azul | Pulso de 40 ms, como máximo una vez por segundo | Alerta de API/HA |
| Batería baja y HA no disponible | Rojo/azul | Pulsos alternos de 40 ms, como máximo aproximadamente una vez por segundo | Ambas alertas |
| Vista previa de color | Canales RGB | Controlado por la vista previa | La vista previa posee temporalmente los canales |

El umbral de batería baja es `< 20%` por defecto. La alimentación externa tiene prioridad sobre los modos de batería baja y alerta de HA. La alerta de HA se basa en `DEMO` confirmado, no en un estado transitorio de conexión. El wake periódico puede generar el mismo pulso corto de alerta.

La actividad del LED no informa actividad ni crea una fuente de wake. Los pulsos normales de alerta se suprimen mientras el sleep está pendiente, durante la recuperación TIMER y mientras una vista previa de color posee los canales. El `status_led_periodic_pulse` separado se permite intencionadamente durante la recuperación TIMER aunque el sleep siga pendiente; la actividad espera a que termine su pulso de unos 40 ms antes de volver a light sleep. Las actualizaciones de estado no se ejecutan mientras el ESP32 duerme y el estado de vista previa no se conserva tras reiniciar.
