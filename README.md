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
