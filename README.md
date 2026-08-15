# ESPHome Davey Spa Controller

> [!CAUTION]
> **Unofficial community project.** This software is not produced, approved, supported, or endorsed by Davey Water Products or ESPHome. Davey and related product names may be trademarks of their respective owners. Use this project entirely at your own risk.

An ESPHome external component for selected spa/pool controller functions over an observed proprietary RS-485 interface. It exposes pump switches, a heater-pump mode selector, a blower cycle control, pool and set-point temperatures, the raw controller display, and menu/lighting key presses to Home Assistant.

## Supported entities

| Entity | Home Assistant type | Feedback | Confidence |
| --- | --- | --- | --- |
| Pump A | Switch | Binary controller state | Physically verified |
| Pump B | Switch | Binary controller state | Physically verified |
| Heater Pump | Select: `OFF`, `AUTO`, `LOW` | Binary controller state | Physically verified |
| Blower Cycle | Button | Input acknowledgement | Observed |
| Blower State | Text sensor | Controller display text | Observed, not a verified binary state |
| Pool Temp | Sensor (°C) | Parsed from controller display | Observed |
| Set Temp | Sensor (°C) | Parsed from controller display | Observed |
| Display Line 1 | Text sensor | Raw controller display line | Observed |
| Display Line 2 | Text sensor | Raw controller display line | Observed |
| Menu Up / Down / Select | Button | Input acknowledgement | Guarded, acts on current display context |
| Light Intensity / Mode | Button | Input acknowledgement | Observed |

All entities except the temperatures, display lines, and menu/lighting buttons are optional in the schema, so a configuration can expose only what it needs.

The component waits for the controller's command-`0x30` response slot, transmits a framed command-`0x80` virtual key press, checks command-`0x38` acknowledgement, and releases the key. Pump and heater-pump changes additionally require matching command-`0x30` state feedback. The transmit checksum is the XOR of every logical byte (command plus payload), matching the receive parser.

## Important limitations

- Compatibility has only been established against the controller traffic used during development. Product branding alone does not prove compatibility.
- This is a reverse-engineered protocol, not a vendor specification.
- The blower is a cycle button, not an absolute on/off switch. Its status is inferred from display text.
- Pool and set-point temperatures are parsed from the controller's display text, not from a dedicated binary field. They update only while the controller shows them.
- Menu and lighting buttons are press-only. They act on whatever screen the controller is currently showing; use the display-line sensors for context. Setpoint editing, schedules, and unknown protocol fields are intentionally not decoded.
- The exact spa connector pinout, bus polarity, voltage, grounding, and termination are installation-specific and are not defined by this project.
- Pool and spa equipment can involve mains voltage, pumps, heaters, and unsafe water temperatures. Do not work on energized equipment and do not rely on this software as a safety control.

## Hardware

The example configuration targets an ESP32-WROOM-32D-compatible development board and a **3.3 V-compatible half-duplex RS-485 transceiver**, such as a MAX3485-class device.

| ESP32 pin | Transceiver signal | Purpose |
| --- | --- | --- |
| GPIO16 | RO | UART receive |
| GPIO17 | DI | UART transmit |
| GPIO4 | DE and `/RE` tied together | High while transmitting, low while receiving |
| 3.3 V | VCC | Use only with a 3.3 V-compatible transceiver |
| GND | GND/reference | Common signal reference where electrically appropriate |

Connect transceiver A/B only after independently identifying the controller bus and checking its electrical levels. Do not connect RS-232 signals or an unlevelled 5 V transceiver to ESP32 GPIO.

## Install with Home Assistant ESPHome

1. Install the ESPHome Device Builder add-on in Home Assistant.
2. Add `wifi_ssid`, `wifi_password`, and `fallback_password` to the ESPHome `secrets.yaml` file. Use [`secrets.yaml.example`](secrets.yaml.example) as the field reference.
3. Add a new ESPHome device and replace its configuration with [`pool-controller.yaml`](pool-controller.yaml), or copy that file into the ESPHome configuration directory.
4. Adjust `uart_rx_pin`, `uart_tx_pin`, and `rs485_direction_pin` if your wiring differs.
5. Select **Install** in ESPHome. Use USB for the first installation when the board is connected to the Home Assistant host, or download the firmware and flash it from a computer.
6. After the device joins Wi-Fi, subsequent installations can use ESPHome OTA.
7. Connect to the spa RS-485 bus only after the ESP32 is running and the transceiver wiring has been verified.

ESPHome downloads the external component from this public repository during validation and compilation:

```yaml
external_components:
  - source: github://crestall/davey-esphome-spa-controller@main
    components:
      - pool_controller
    refresh: 0s
```

Pin a release tag instead of `main` for a stable installation once releases are available.

## Home Assistant dashboard

A ready-made Lovelace dashboard is included at [`dashboard.yaml`](dashboard.yaml). It provides an SP1200-style two-line display widget, pool/set-point temperature gauges, keypad-style pump and blower buttons, a menu navigation row, lighting buttons, and full equipment and controller-feedback lists.

1. In Home Assistant, go to **Settings -> Dashboards -> Add Dashboard -> New dashboard from scratch**.
2. Open the new dashboard, select the edit (pencil) icon, then the three-dot menu, and choose **Edit in YAML** (raw configuration editor).
3. Paste the contents of [`dashboard.yaml`](dashboard.yaml).
4. Adjust the entity ID prefix to match your device. Home Assistant usually prefixes ESPHome entities with the device (and sometimes area) name, for example `switch.davey_spa_controller_pump_a`. Confirm the exact IDs under **Settings -> Devices & Services** or **Developer Tools -> States**.

### Stale external component cache

If a build log still shows `class PoolController : public Component, public uart::UARTDevice`, ESPHome is compiling an obsolete cached copy. Current `main` uses `UARTComponent` composition and does not contain that declaration.

1. Set `refresh: 0s` in the `external_components` block as shown above.
2. In ESPHome Device Builder, select **Clean Build Files** for the device.
3. Validate or install again. The log should show ESPHome cloning or fetching this repository before generating C++.

The `refresh: 0s` setting is appropriate while following `main`. A future tagged release can use a longer refresh interval because its source will be immutable.

## How the protocol was obtained

No vendor protocol documentation or firmware source was used. The interface was derived from traffic on equipment available for testing:

1. Traffic was captured passively at candidate serial settings until consistent packets were visible.
2. Repeated captures established `19200 8N1` and a proprietary STX/ETX packet structure.
3. Controlled changes on the original panel were correlated with packet differences.
4. Framing, DLE byte escaping, and the XOR checksum were independently reproduced.
5. Individual virtual-key payloads were tested one at a time while observing the physical equipment.
6. Command-`0x30` state changes were correlated with Pump A, Pump B, and heater-pump states.
7. Command timing was measured and transmissions were restricted to the response window following command `0x30`.
8. Acknowledgements and resulting equipment state were separated so an accepted key press was not mistaken for a successful physical transition.
9. Unknown fields were left unmapped, and controls without reliable context or feedback were omitted.

See [Protocol discovery and confidence](docs/protocol-discovery.md) for the technical record.

## Repository layout

```text
components/pool_controller/  ESPHome Python schema and C++ component
pool-controller.yaml         Home Assistant ESPHome configuration
dashboard.yaml               Home Assistant Lovelace dashboard
secrets.yaml.example         Credential field template only
docs/                        Discovery method and protocol notes
```

## Development validation

From an ESPHome Python environment:

```text
esphome config pool-controller.yaml
esphome compile pool-controller.yaml
```

A complete compile requires the ESPHome toolchain and Internet access. Never commit `secrets.yaml`, `.esphome`, build output, or virtual environments.

## License

Released under the [MIT License](LICENSE). This license applies to this project's original source code only and grants no rights to third-party trademarks, hardware designs, firmware, or proprietary documentation.
