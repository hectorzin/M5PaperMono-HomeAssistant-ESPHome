# Primeros pasos

[English](../Getting_Started.md) | [Español](Getting_Started.md)

Esta es la ruta de instalación recomendada para usuarios de Home Assistant. Usa ESPHome Device Builder y el paquete compartido publicado en GitHub; no es necesario clonar este repositorio ni copiar `packages/`, `components/`, `custom_components/` o `external_components/`.

El idioma predeterminado del firmware es `language: "en"`. Establece `language: "es"` para seleccionar español. Cambiar el idioma requiere recompilar e instalar el firmware. ESPHome deriva actualmente los IDs de entidad públicos de los valores visibles de `name:`, por lo que los nombres traducidos pueden producir otros valores de `entity_id` al instalar el firmware nuevo. Los IDs internos del firmware permanecen estables en inglés, y los `entity_id` de Home Assistant y nombres de `controls:` proporcionados por el usuario nunca se traducen.

El idioma elegido para leer esta documentación es independiente de `language`. La selección `language: "en"` o `language: "es"` controla el idioma del firmware en tiempo de compilación.

## Requisitos

- M5Stack PaperMono C153
- ESPHome Device Builder en Home Assistant
- Wi-Fi local y Home Assistant con la integración de ESPHome
- Varios gigabytes de almacenamiento libre; la primera compilación de ESP-IDF/PlatformIO puede requerir unos 10 GB

El proyecto apunta a un ESP32-S3 con 16 MB de flash y PSRAM octal. El componente de tinta electrónica está fijado a 800×480.

## Instalación recomendada en Home Assistant

1. Instala o abre **ESPHome Device Builder** en Home Assistant.
2. Crea un dispositivo ESP32 nuevo y abre su editor YAML.
3. En el `secrets.yaml` de Builder, añade las credenciales usando placeholders; nunca pongas una contraseña real en documentación compartida:

   ```yaml
   wifi_ssid: "TU_WIFI"
   wifi_password: "TU_PASSWORD"
   ```

4. En el YAML del dispositivo, usa esos secrets:

   ```yaml
   substitutions:
     wifi_ssid: !secret wifi_ssid
     wifi_password: !secret wifi_password
   ```

5. Añade los IDs de entidad de Home Assistant que quieras mostrar o controlar. Consulta [Configuración de Home Assistant](Home_Assistant_Configuration.md) y [Habitaciones y controles](Rooms_and_Controls.md).
6. Opcionalmente configura los bloques de control y los IDs NFC descritos en [NFC](NFC.md).
7. Conserva el bloque de paquete remoto en el YAML:

   ```yaml
   packages:
     paper_mono:
       url: https://github.com/hectorzin/M5PaperMono-HomeAssistant-ESPHome
       ref: main
       files:
         - packages/paper_mono_base.yaml
       refresh: 0s
   ```

   Los includes relativos dentro de `paper_mono_base.yaml` se resuelven dentro del checkout del repositorio remoto. `refresh: 0s` permite que las compilaciones comprueben la versión actual de `main` en lugar de conservar indefinidamente una copia obsoleta del paquete.

8. Guarda el YAML y valídalo en ESPHome Builder.
9. Realiza la primera instalación por USB si el dispositivo todavía no es accesible por Wi-Fi.
10. Cuando el dispositivo esté conectado, instala las actualizaciones posteriores por OTA.

El dispositivo expone la luz frontal, el voltaje y nivel de batería, el estado de alimentación externa, el último UID NFC y las entidades de configuración en tiempo de ejecución. Las tarjetas de control no crean entidades adicionales de Home Assistant: consumen y manejan las entidades existentes configuradas en el YAML del dispositivo. Consulta [Configuración de Home Assistant](Home_Assistant_Configuration.md).

## Actualización del firmware

Como el YAML consume `main` con `refresh: 0s`, vuelve a compilar e instalar desde ESPHome Builder para obtener una versión publicada nueva del proyecto. No necesitas copiar otra vez los paquetes ni los componentes. Puedes usar OTA cuando el dispositivo sea accesible.

## Notas de desarrollo

La estructura del repositorio, el árbol local `components/` y los comandos CLI están pensados para desarrollo y solución de problemas del firmware, no para la instalación normal de Home Assistant. Consulta [Arquitectura](Architecture.md) al trabajar en la implementación.

Si no se pueden usar las credenciales Wi-Fi, el AP alternativo configurado es `${device_name}-setup`, protegido por la contraseña secreta de Wi-Fi. En Windows, las rutas de proyecto con caracteres no ASCII pueden causar errores al cargar fuentes; usa una ruta ASCII si ocurre.
