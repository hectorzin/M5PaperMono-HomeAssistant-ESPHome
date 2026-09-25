# Pantalla y actualizaciones

[English](../Display_and_Refresh.md) | [Español](Display_and_Refresh.md)

El controlador personalizado es una implementación monocroma fija de SSD1677 de 800×480 que usa las formas de onda OTP de PaperMono. SPI es de solo escritura a 20 MHz. Una actualización PARTIAL transfiere el framebuffer RAM1 completo; las regiones lógicas modificadas se conservan actualmente solo para logs y diagnóstico y no deciden entre FULL/PARTIAL.

## Política

- **FULL:** establece o restablece el baseline físico de partial.
- **PARTIAL:** actualiza usando el baseline existente.
- **Promoción a FULL físico:** una solicitud normal se resuelve como FULL cuando el baseline no es válido o se ha alcanzado su umbral de política.
- **`RefreshKind::MANDATORY_FULL`:** tipo de solicitud explícito usado para arranque/recuperación del PMIC y rutas de limpieza.
- **Barrera de recuperación del PMIC:** bloquea o aplaza las actualizaciones normales hasta que la recuperación de hardware permita el FULL inicial requerido.

Una solicitud normal con baseline no válido se promociona a FULL físico; no se descarta simplemente como PARTIAL rechazado. Las solicitudes durante BUSY se agrupan. Un tipo FULL obligatorio prevalece sobre uno normal; al combinar políticas automáticas y de interacción de usuario se conserva la automática porque tiene el umbral FULL menor. Se siguen hasta cuatro regiones parciales lógicas para diagnóstico. Antes de light sleep, la actividad espera tanto a un panel inactivo como a que no haya actualizaciones pendientes.

El timeout de BUSY es de 15 segundos. Un timeout invalida el baseline, marca la recuperación de hardware como fallida/pendiente y registra `BUSY timeout`. Las solicitudes normales posteriores quedan bloqueadas. El componente no tiene un bucle general de reintento automático para esta condición; la recuperación requiere un reinicio o la ruta explícita de recuperación de hardware del PMIC.

Las actualizaciones pueden originarse en el arranque, cambios de estado de Home Assistant, acciones de controles, wake periódico o recuperación del PMIC. La política general promociona las solicitudes normales a FULL cuando el contador partial alcanza 10 para la política automática o 15 para la política de interacción de usuario. Las rutas explícitas de limpieza actúan antes: pickup a 8 partials, entrada en controls a 8 y salida de controls a 10 solicitan `MANDATORY_FULL`. Un baseline no válido promociona una solicitud normal a FULL físico, mientras que la recuperación de arranque/PMIC usa el tipo obligatorio y la barrera descritos arriba. La opción YAML `full_update_every` permanece en el esquema y está configurada a `0` en `packages/ui.yaml`, pero la política actual ignora ese valor; se siguen aplicando los umbrales centrales y las rutas explícitas de limpieza.

Home Assistant proporciona la zona horaria y el reloj. El panel redondea el minuto mostrado hacia abajo al intervalo configurado: por ejemplo, `12:14` se dibuja como `12:10` con un intervalo de cinco minutos. Durante un timeout de wake periódico se usan la hora local y los valores de HA conservados en RAM durante light sleep; esa caché no está disponible tras un arranque en frío. Un reinicio o la pérdida completa de alimentación del SSD1677 invalida el baseline físico anterior, y la recuperación del PMIC ejecuta el FULL obligatorio antes de permitir actualizaciones PARTIAL.
