# Rooms and Controls

## Configuration model

Controls are declared in `paper_mono.yaml` under `controls.blocks`. Each block requires a name and an `entities` list, but the current schema permits that list to be empty. `nfc_id` is optional.

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

`entity_id` is required. `name` is optional; when omitted, the Home Assistant `friendly_name` is used, falling back to a humanized object ID with the domain removed and underscores replaced by spaces. NFC IDs are normalized with `strip().upper()`, so letter case and surrounding whitespace in YAML do not matter; separators are not removed. Duplicate non-empty NFC IDs are rejected.

Supported domains are exactly `light`, `switch`, `cover`, `climate`, `vacuum`, and `media_player`. Other domains become unsupported slots and generate a warning. Each block schema accepts at most 48 entities, and the flattened control list is also validated at 48 entries, so the effective total limit for the complete configuration is 48 controls. Six cards are displayed per page in a 2-column × 3-row grid; entities remain in YAML order.

The six-card limit comes from the implementation, not from a configurable YAML option: `Controls::control_index_at_page_slot()` accepts visual slots `0..5`, the renderer loops over six slots, and page counts are calculated as `(controls_in_block + 5) / 6`.

## Navigation

Touching the dashboard requests entry to controls. If Wi-Fi/API/HA are not ready, entry remains pending on the connecting view and completes when HA becomes ready. The top back area returns home. GPIO2/GPIO3 normally navigate pages while controls are active, and enter the first/last page from the dashboard. A current implementation limitation prevents GPIO page changes when the flattened list has six or fewer controls, even if multiple blocks produce more than one page. An NFC match opens the matching block's first page.

## Interactions

- **Light:** toggle, brightness, and—when reported—RGB/color-temperature editing.
- **Switch:** toggle.
- **Climate:** lower/raise target temperature and choose the reported modes supported by the popup: `cool`, `heat`, `fan_only`, `dry`, `auto`, `heat_cool`, and `off`. The current popup only works for climate controls whose global flattened index is below 6; later climate controls can still change target temperature but cannot use the mode popup.
- **Cover:** open, stop, or close.
- **Vacuum:** start, pause, or return to base.
- **Media player:** volume, previous, play/pause, and next when supported.

Actions require a ready native API connection. The cards subscribe to and operate existing Home Assistant entities; they do not create new control entities. Displayed values come from Home Assistant state cached in RAM.

The order of each YAML entity list determines visual order; there is no independent `order` field. There is also no YAML support for arbitrary per-control icons or a custom grid. Layout and icons are selected by the renderer from the domain and reported attributes.
