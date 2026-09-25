# Habitaciones y controles

[English](../Rooms_and_Controls.md) | [Español](Rooms_and_Controls.md)

## Modelo de configuración

Los controles se declaran en el YAML del dispositivo bajo `controls.blocks` (el ejemplo del repositorio es `paper_mono.yaml`). Cada bloque requiere un nombre y una lista `entities`, aunque el esquema actual permite que esté vacía. `nfc_id` es opcional.

```yaml
controls:
  blocks:
    - name: "Living room"
      nfc_id: "A145E804"
      entities:
        - entity_id: light.sofa
          name: Sofa
        - entity_id: climate.living_room
          name: Climate
        - entity_id: cover.living_room_blind
          name: Blind
```

`entity_id` es obligatorio. `name` es opcional; si se omite, se usa el `friendly_name` de Home Assistant y, como alternativa, un object ID humanizado con el dominio eliminado y los guiones bajos sustituidos por espacios. Los IDs NFC se normalizan con `strip().upper()`, por lo que no importan las mayúsculas/minúsculas ni los espacios exteriores del YAML; los separadores no se eliminan. Los IDs NFC no vacíos duplicados se rechazan.

Los dominios admitidos son exactamente `light`, `switch`, `cover`, `climate`, `vacuum` y `media_player`. Los demás dominios se convierten en slots no compatibles y generan una advertencia. Cada bloque admite como máximo 48 entidades y la lista de controles aplanada también se valida con 48 entradas, por lo que el límite total efectivo de la configuración completa es de 48 controles. Se muestran seis tarjetas por página en una cuadrícula de 2 columnas × 3 filas; las entidades conservan el orden del YAML.

El límite de seis tarjetas procede de la implementación, no de una opción YAML configurable: `Controls::control_index_at_page_slot()` acepta los slots visuales `0..5`, el renderizador recorre seis slots y el número de páginas se calcula como `(controls_in_block + 5) / 6`.

## Navegación

Tocar el panel solicita entrar en controls. Si Wi-Fi/API/HA no están listos, la entrada queda pendiente en la vista de conexión y se completa cuando HA está disponible. La zona superior de volver regresa al inicio. GPIO2/GPIO3 normalmente navegan entre páginas mientras controls está activo, y entran en la primera/última página desde el panel. Una limitación de la implementación actual impide cambiar de página con GPIO cuando la lista aplanada tiene seis controles o menos, aunque varios bloques produzcan más de una página. Una coincidencia NFC abre la primera página del bloque correspondiente.

## Interacciones

- **Light:** alternar, brillo y —cuando se informa— edición RGB/temperatura de color.
- **Switch:** alternar.
- **Climate:** bajar/subir la temperatura objetivo y elegir los modos informados compatibles con el popup: `cool`, `heat`, `fan_only`, `dry`, `auto`, `heat_cool` y `off`. El popup actual solo funciona para controles climate cuyo índice global aplanado sea menor que 6; los controles climate posteriores aún pueden cambiar la temperatura objetivo, pero no usar el popup de modo.
- **Cover:** abrir, detener o cerrar.
- **Vacuum:** iniciar, pausar o volver a la base.
- **Media player:** volumen, anterior, reproducir/pausar y siguiente cuando estén disponibles.

Las acciones requieren una conexión API nativa lista. Las tarjetas se suscriben a entidades existentes de Home Assistant y trabajan sobre ellas; no crean nuevas entidades de control. Los valores mostrados proceden del estado de Home Assistant almacenado en RAM.

El orden de cada lista de entidades YAML determina el orden visual; no existe un campo `order` independiente. Tampoco hay soporte YAML para iconos arbitrarios por control ni para una cuadrícula personalizada. El renderizador selecciona el diseño y los iconos a partir del dominio y de los atributos informados.
