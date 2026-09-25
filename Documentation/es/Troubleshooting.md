# Solución de problemas

[English](../Troubleshooting.md) | [Español](Troubleshooting.md)

## No hay conexión con Home Assistant

Comprueba `wifi_ssid`, `wifi_password`, DHCP, la accesibilidad de la red y los logs de ESPHome. HEAD usa DHCP; solo existe una dirección fija si el usuario añade una personalización opcional `manual_ip`. El estado de la API nativa es la autoridad para controles y alertas. Los datos almacenados en RAM aún pueden mostrarse después de light sleep, pero las acciones quedan bloqueadas hasta que la API esté lista.

## Wi-Fi no se recupera después de sleep

Busca `Enabling WiFi after light sleep`, `Periodic wake: API ready` o `Periodic wake: network/API timeout`. El wake TIMER espera hasta 30.000 ms a Wi-Fi/API y después usa un periodo de estabilización de 1.750 ms. Tras el timeout, el firmware actual actualiza usando la hora local y los datos de HA conservados en RAM antes de light sleep, y vuelve a light sleep. NFC permanece intencionadamente apagado durante toda esta recuperación periódica.

## La pantalla está obsoleta o no cambia

Inspecciona `EPD physical refresh request`, `PARTIAL refresh`, `FULL refresh` y `BUSY timeout`. Las solicitudes pueden agruparse mientras el panel está ocupado. Después de perder la alimentación del PMIC, la primera actualización válida debe ser un FULL obligatorio.

## Timeout BUSY de la tinta electrónica

El timeout es de 15 segundos. Comprueba la alimentación de la pantalla, reset, cableado BUSY y disponibilidad de M5IOE1. Un timeout invalida el baseline partial y bloquea las actualizaciones normales posteriores. No hay un bucle general de recuperación automática para este estado; puede ser necesario reiniciar o usar la ruta explícita de recuperación de hardware del PMIC.

## NFC no detecta una tarjeta

Comprueba `NFC power ON`, `NFC IRQ enabled` y `NFC low-power detection armed`. Confirma la dirección `0x50`, SDA/SCL GPIO47/GPIO48, M5IOE1 GPIO4 e IRQ GPIO6. NFC está intencionadamente apagado durante light sleep, la recuperación periódica TIMER y el apagado del PMIC; se reinicializa después del wake de usuario por táctil/movimiento.

## El UID no se reconoce

Usa hexadecimal sin separadores en `nfc_id`. No es necesario usar mayúsculas porque la configuración aplica `strip().upper()`, pero los separadores no se eliminan. Una tarjeta desconocida confirmada aún actualiza `Última tarjeta NFC`, pero no navega. Compara el log del UID con la configuración del bloque.

## El dispositivo no duerme o no despierta

El sleep espera a que la pantalla esté inactiva y no haya actualizaciones pendientes. Una actualización en cola, una recuperación, actividad del usuario o un override de usuario de horas silenciosas activo puede retrasarlo. En light sleep, prueba táctil y movimiento por separado. Una solicitud `SLEEP_TIMEOUT` o `sleep_now` sigue siendo una ruta de light sleep aunque el reloj esté dentro de las horas silenciosas. Durante el apagado real del PMIC, el táctil no puede despertar el dispositivo; usa RTC, movimiento o POWER.

## El wake periódico no tiene HA

Es un comportamiento de fallback compatible. El log `refreshing with local time and cached HA state` significa que el dispositivo alcanzó el timeout de 30.000 ms, usará el estado almacenado en RAM después del periodo de estabilización de 1.750 ms, actualizará y volverá a dormir. Diagnostica los fallos persistentes como problemas de Wi-Fi/API.

## Horas silenciosas y batería

El planificador normal de horas silenciosas usa el apagado del PMIC, pero `SLEEP_TIMEOUT` y `sleep_now` permanecen deliberadamente en rutas de light sleep. Solo el apagado real del PMIC garantiza que no se ejecute el wake periódico de la tinta electrónica. Valores de inicio y fin iguales desactivan las horas silenciosas. Comprueba las dos entidades `Paper Mono Quiet Hours ...` y el reloj. Con USB/5VIN presente, el apagado del PMIC puede volver a alimentar inmediatamente el ESP32. Para la batería, comprueba `Paper Mono Battery Voltage`, `Paper Mono Battery Level` y `Paper Mono External Power`. La presentación de batería usa una media móvil de tres muestras; la batería baja es inferior al 20% por defecto y produce un pulso rojo de unos 40 ms como máximo aproximadamente una vez por segundo.

## Compilación de ESPHome

En ESPHome Device Builder, usa primero **Validate** y confirma que `secrets.yaml`, el bloque de paquete remoto y una versión compatible de ESPHome estén disponibles. La primera descarga de ESP-IDF/PlatformIO es grande. El comando CLI `esphome config paper_mono.yaml` solo se aplica a un checkout local de desarrollo, no a la instalación normal de Home Assistant. En Windows, usa una ruta de proyecto ASCII si falla la carga de fuentes.
