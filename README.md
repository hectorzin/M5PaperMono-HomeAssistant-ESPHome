# M5Stack PaperMono C153 — ESPHome (fase 1)

Firmware mínimo de prueba de hardware: e-paper SSD1677 + touch FT6336G vía M5IOE1.

## Requisitos

- [ESPHome](https://esphome.io/) 2026.6.x o superior
- M5Stack PaperMono C153
- Red Wi-Fi local (no se requiere Home Assistant para esta fase)

## Configuración

```bash
cp secrets.example.yaml secrets.yaml
# Editar secrets.yaml con tu SSID y contraseña Wi-Fi
```

La fuente `fonts/DejaVuSans.ttf` (licencia libre DejaVu) se incluye en el repositorio para el renderizado del display.

> **Nota Windows:** si `esphome config` falla al cargar la fuente con error “not a valid font file”, puede deberse a caracteres no ASCII en la ruta del proyecto (p. ej. nombre de usuario). Compila desde una ruta sin acentos o usa un enlace simbólico (`mklink /J C:\papermono-ha <ruta-proyecto>`).

La primera compilación ESPHome/ESP-IDF descarga varios GB de toolchain; requiere espacio libre en disco (recomendado ≥ 10 GB en la unidad de PlatformIO, por defecto `C:\.platformio`).

## Estructura de configuracion

`paper_mono.yaml` es el punto de entrada y enumera los paquetes que forman el firmware. Las entidades que debe adaptar cada usuario permanecen centralizadas en `config/home_assistant.yaml`. La configuracion funcional se organiza en `packages/` (nucleo, conectividad, hardware, Home Assistant y UI), mientras que `components/papermono_epaper/` conserva el driver e-paper especifico.

## Validar y compilar (sin flashear)

```bash
esphome config paper_mono.yaml
esphome compile paper_mono.yaml
```

## Orientación (hipótesis de prueba)

M5GFX configura el panel PaperMono con `offset_rotation = 3` (800×480 nativo) y el touch con rango raw 480×800. **Este YAML no aplica `rotation` ni `mirror`** en display ni touch: la primera ejecución sirve para observar orientación, espejo y correspondencia touch/pantalla en hardware real.

## M5IOE1 — numeración ESPHome

| Función        | Nombre oficial  | `number` |
|----------------|-----------------|----------|
| E-paper power  | M5IOE1_PIN_3    | 2        |
| E-paper reset  | M5IOE1_PIN_5    | 4        |
| Touch reset    | M5IOE1_PIN_6    | 5        |
| Touch power    | M5IOE1_PIN_13   | 12       |

Dirección I2C del PaperMono: `0x4F`.

## Notas técnicas

- **EPD power:** `epaper_spi` declara `enable_pin` en el esquema YAML pero no lo implementa en C++ (ESPHome 2026.6.5). La alimentación del panel se enciende con un `switch` GPIO en M5IOE1 (`restore_mode: ALWAYS_ON`).
- **EPD reset:** conectado directamente a `reset_pin` del display (M5IOE1 `number: 4`).
- **Touch power:** `switch` GPIO M5IOE1 `number: 12` con `restore_mode: ALWAYS_ON` (FT63x6 no tiene pin de enable).
- **Touch reset:** conectado a `reset_pin` del touchscreen (M5IOE1 `number: 5`).

## Gestión de energía

Esta sección describe el comportamiento implementado actualmente en el firmware.

### Frontlight y actividad

El frontlight permanece apagado normalmente y se enciende al 30% cuando hay
actividad explícita:

- movimiento detectado por el BMI270;
- interacción táctil.

Permanece encendido mientras continúe la actividad y se apaga después del
**Frontlight Timeout** (30 segundos por defecto) sin nueva actividad. Un nuevo
movimiento lo enciende inmediatamente. El movimiento llega por la ruta
`BMI270 INT1 -> M5PM1 GPIO4 -> ESP32 GPIO1` durante el funcionamiento normal.
El brillo por defecto es configurable; no es un brillo automático.

### Refresco de la e-paper

El panel es un SSD1677 y usa los waveforms OTP del PaperMono. Como referencia,
un PARTIAL tarda aproximadamente 0,9 s y un FULL aproximadamente 4,3 s.

Aunque el resultado visual sea parcial, cada PARTIAL envía el framebuffer
completo a RAM1. Los FULL se usan estratégicamente para limpiar ghosting y
restablecer una base física válida. El contador `partial_count` ayuda a las
políticas de interacción (por ejemplo, pickup y controles) a decidir cuándo
solicitar un FULL; durante el refresco normal de la pantalla principal no hay
un FULL automático periódico por ese contador.

### Funcionamiento normal durante el día

Mientras el usuario interactúa, el ESP32 permanece despierto. Tras el
**Frontlight Timeout** se apaga el frontlight, pero el dispositivo sigue
despierto con Wi-Fi y API activos. Cuando vence el **Sleep Timeout** (60
segundos por defecto) sin nueva actividad desde el último touch o movimiento,
el firmware:

1. sale de la vista de controles si estaba activa;
2. espera a que el EPD quede idle tras cualquier PARTIAL pendiente;
3. apaga Wi-Fi;
4. entra en **ESP32 light sleep**.

Ambos plazos se miden de forma independiente desde la misma última actividad:
apagar el frontlight no reinicia el Sleep Timeout.

El **Refresh Interval** (`screensaver_refresh_minutes`, 5 minutos por defecto)
no determina cuándo entra el dispositivo en light sleep. Solo programa los
despertares periódicos **una vez ya dormido**: al vencer el temporizador, el
firmware se despierta, actualiza la pantalla y vuelve a light sleep si no hay
actividad.

Durante el light sleep puede producirse un **wake** por el temporizador del
siguiente tick absoluto alineado o por movimiento del BMI270.

Después de un wake por temporizador, se vuelve a habilitar Wi-Fi, se espera la
conexión/API y la recuperación de estados, se actualiza la pantalla y se vuelve
a light sleep si no ha habido actividad. Después de un wake por movimiento, el
dispositivo vuelve inmediatamente al funcionamiento activo, enciende el
frontlight y aplica la política normal de pickup/refresco.

Un nuevo touch o movimiento cancela un sleep pendiente y reinicia ambos plazos
de inactividad.

Si el ESP32 está despierto cuando llega `quiet_hours_start`, el inicio exacto
puede depender del scheduler: la transición puede producirse en el siguiente
tick alineado. Mientras ya está en light sleep, el temporizador sí puede
programarse para tener en cuenta `quiet_hours_start`. Si el Sleep Timeout vence
durante quiet hours, no se usa light sleep: se aplica la ruta de **M5PM1
shutdown** con pantalla `ZZZ` y luna.

### Quiet hours: M5PM1 shutdown

Las quiet hours se configuran mediante estas substitutions:

```yaml
quiet_hours_start: "00:00"
quiet_hours_end: "08:00"
```

Sus valores actuales por defecto son `00:00` y `08:00`. Durante ese intervalo
no se usa ESP deep sleep: se ejecuta **M5PM1 SHUTDOWN**.

Al entrar en quiet hours, el firmware actualiza por última vez la batería,
muestra la pantalla de reposo `ZZZ` con la luna, espera a que termine el
refresco del EPD, programa el RTC para `quiet_hours_end`, mantiene L1 del M5PM1
para conservar el RTC y el BMI270, y ejecuta el shutdown. El ESP32, Wi-Fi y la
mayor parte del hardware quedan realmente apagados; no hay refrescos
periódicos durante la noche.

La secuencia nocturna debe entenderse como:

```text
M5PM1 SHUTDOWN -> POWER-ON / COLD BOOT DEL ESP32
```

No es un wake normal del ESP32 desde light sleep.

### Cómo vuelve a encenderse durante quiet hours

Se han probado físicamente estas tres fuentes de power-on:

| Fuente | Funcionamiento | Clasificación |
|---|---|---|
| RTC | Al llegar `quiet_hours_end`, el RX8130 genera la señal y el M5PM1 vuelve a alimentar el equipo. | `RTC` |
| Movimiento | El BMI270 permanece alimentado mediante L1; la señal llega a M5PM1 GPIO4 y el PMIC vuelve a encender el equipo. | `MOTION` |
| Botón POWER | El botón físico POWER vuelve a encender el equipo desde M5PM1 shutdown. | `POWER_BUTTON` |

El movimiento y el botón POWER se consideran intención explícita del usuario y
encienden el frontlight. Para distinguir un `POWER_BUTTON` posterior a nuestro
quiet-hours shutdown de un cold boot normal mediante botón, el firmware combina
`WAKE_SRC PWRBTN` con un marcador persistente escrito antes del shutdown.

El touch no puede encender el dispositivo desde M5PM1 shutdown: el FT6336G no
permanece en el dominio alimentado necesario y su INT llega al ESP32 apagado.
Una vez completado el power-on, el touch vuelve a funcionar normalmente.

### Recuperación después de M5PM1 shutdown

Después de un wake por `RTC`, `MOTION` o `POWER_BUTTON` se realiza un boot nuevo
del ESP32. El firmware detecta que procede de nuestro M5PM1 shutdown, recupera
M5IOE1, vuelve a alimentar y resetear el EPD y el touch, y recupera el PWM del
frontlight.

Antes de permitir cualquier PARTIAL físico se ejecuta un FULL obligatorio. Al
haber desaparecido la alimentación del SSD1677, ya no se puede asumir que
existe una base válida para PARTIAL.

```text
M5PM1 power-on
  -> recuperación del hardware
  -> FULL obligatorio (~4,3 s)
  -> conexión con Home Assistant
  -> recuperación y settle de estados
  -> FULL final con los datos definitivos de HA
  -> partial_count = 0
  -> funcionamiento normal
```

Los dos FULL son intencionados: el primero establece una base física segura
del SSD1677 y el segundo pinta el estado definitivo de Home Assistant y deja
limpio el contador de parciales. Entre ambos pueden actualizarse los datos de
HA; eso es normal. Lo único que se bloquea internamente es cualquier PARTIAL
físico antes del FULL obligatorio.

Si el usuario levanta el dispositivo o pulsa POWER durante quiet hours, arranca
normalmente con la UI normal (no `ZZZ`) y el frontlight activo por la
interacción. Puede utilizarse con normalidad. Cuando vuelve a quedar inactivo,
el frontlight se apaga tras el Frontlight Timeout y, al vencer el Sleep Timeout
sin nueva actividad, se muestra `ZZZ`, se rearma el RTC y se vuelve a M5PM1
shutdown mientras siga dentro de quiet hours.

### Estados de energía

| Estado | Características |
|---|---|
| **ACTIVE** | ESP32 encendido, Wi-Fi encendido, pantalla y touch disponibles; frontlight según actividad. |
| **DAYTIME LIGHT SLEEP** | ESP32 en light sleep, Wi-Fi apagado; wake por temporizador o movimiento; usado entre actualizaciones diurnas. |
| **QUIET-HOURS M5PM1 SHUTDOWN** | ESP32 y Wi-Fi apagados; RTC y BMI270 mantenidos por L1; power-on por RTC, movimiento o botón POWER; sin refrescos nocturnos. |

### Configuración de energía

Estas son las substitutions disponibles en `paper_mono.yaml`:

```yaml
frontlight_timeout_seconds: "30"
sleep_timeout_seconds: "60"
screensaver_refresh_minutes: "5"
quiet_hours_start: "00:00"
quiet_hours_end: "08:00"
```

Tres plazos distintos gobiernan el ahorro de energía diurno:

| Plazo | Entidad HA | Default | Función |
|---|---|---|---|
| **Frontlight Timeout** | `Paper Mono Frontlight Timeout` | 30 s | Apaga el frontlight tras inactividad. El ESP32 sigue despierto. |
| **Sleep Timeout** | `Paper Mono Sleep Timeout` | 60 s | Tras inactividad, entra en light sleep (o M5PM1 shutdown si ya es quiet hours). Rango 10–3600 s, step 10. |
| **Refresh Interval** | `Paper Mono Refresh Interval` | 5 min | Intervalo de los wakes periódicos **durante** light sleep. No retrasa la entrada inicial en sleep. |

Los dos timeouts se miden de forma independiente desde la misma última
actividad (touch o movimiento). Apagar el frontlight no reinicia el Sleep
Timeout.

- `screensaver_refresh_minutes` debe dividir 60 minutos uniformemente, por
  ejemplo `5`, `10`, `15` o `30`.
- `quiet_hours_start` y `quiet_hours_end` definen la ventana nocturna en
  formato `HH:MM`.

Durante el funcionamiento, estos valores se pueden editar desde Home Assistant
con las entidades de configuración del dispositivo (`Paper Mono Frontlight
Brightness`, `Paper Mono Frontlight Timeout`, `Paper Mono Sleep Timeout`,
`Paper Mono Refresh Interval`, quiet hours y `Paper Mono Restore Defaults`).
Se restauran mediante NVS (`globals` persistentes de ESPHome); por tanto, los
valores de este YAML solo son defaults iniciales. `Paper Mono Restore Defaults`
reaplica todos los defaults compilados.
