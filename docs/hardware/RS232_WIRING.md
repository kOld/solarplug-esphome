# RS232 Wiring

Use a real RS232 level shifter, an RS232 base, or a known-compatible USB RS232 cable. Do not connect inverter RS232 directly to 3.3 V UART pins.

For PowMr HVM KP-series units, Solar Assistant shows the RJ45 `RS232` / `COM` port at the bottom of the inverter and documents the inverter-side pinout as pin 3 RX, pin 4 GND, and pin 6 TX. The POW-HVM6.2KP manual also lists pin 5 as GND, so there may be two ground pins on the inverter COM connector.

![PowMr HVM KP-series RS232 communication port](https://solar-assistant.io/help-images/docs/inverters/datouboss/datouboss-ports.png?v=1773377834)

Image source: [Solar Assistant PowMr HVM KP-series RS232 guide](https://solar-assistant.io/help/inverters/powmr/POW-HVM-KP/rs232).

## Inverter COM Port Pinout

The confirmed PowMr/MrPow `POW-HVM6.2KP` setup uses the inverter RJ45 port labeled `RS232` / `COM`.

| RJ45 pin | Inverter signal | Connects to |
|---:|---|---|
| 1 | NC | not connected |
| 2 | NC / +12 V in manual | leave unconnected |
| 3 | RX / RS232_TXD naming varies by source | RS232 adapter TX |
| 4 | GND | RS232 adapter GND |
| 5 | GND in manual/Cisco-style pinouts | optional tie to RS232 adapter GND |
| 6 | TX / RS232_RXD naming varies by source | RS232 adapter RX |
| 7 | NC | not connected |
| 8 | NC | not connected |

The practical wiring is three signals: inverter pin 3, inverter pin 6, and ground. Use pin 4 for ground; tying pin 5 to the same adapter ground is consistent with the manual and Cisco-style RJ45 serial cables. Do not use pin 2 for ESPHome wiring.

Field reports match the unusual HVM6.2KP data pins: a DIY Solar Forum user reports three working HVM6.2KP units using a short cable, 2400 baud, pin 3 TX, pin 6 RX, and pin 2/4 as voltage rails for their RS232-TTL module. For this project, keep the ESP32 self-powered and leave pin 2 disconnected unless you have measured the port and intentionally designed for that supply.

![RJ45 to USB RS232 cable style used by Solar Assistant](https://solar-assistant.io/help-images/shop/inverter_cables/images/voltronic_rs232-full.png?v=1773377834)

Solar Assistant also notes that these units use the same RJ45 pin configuration as Cisco-style RS232 cables. Those cables are useful for USB hosts. For an ESP32 build, keep the RS232 transceiver between the ESP32 UART and the inverter port.

If the link is silent, verify the connector orientation first, then swap only the adapter TX/RX pair. Never experiment with pin 2 or direct ESP32 GPIO on the inverter RS232 port.

## Confirmed Sniffer Dongle

The local protocol capture was made with a `WIFI-RELAB` dongle connected to a PowMr/MrPow `POW-HVM6.2KP` inverter.

The `WIFI-RELAB` compatibility list groups these inverter models together:

- `POW-HVM6.2KP`
- `POW-HVM11KP`
- `POW-RELAB 3KU`
- `POW-RELAB 3KE`
- `POW-RELAB 5KU`
- `POW-RELAB 5KE`
- `POW-RELAB 10KU`
- `POW-RELAB 10KE`
- `POW-RELAB SPLIT Series`

Treat non-local models as likely compatible, not confirmed, until captures prove the command set.

## Solar Plug-RWB1-06R Pinout

Public Solar Plug-RWB1 documentation lists the `-06R` dongle/cable variant as:

| Pin | Signal |
|---:|---|
| 1 | NC |
| 2 | VCC |
| 3 | RXD |
| 4 | NC |
| 5 | GND |
| 6 | TXD |
| 7 | NC |
| 8 | NC |

Confirm your own cable before powering anything. Different RWB1 submodels use different pinouts.

Do not mix this RWB1 variant table with the inverter COM port table above. The active replacement wiring should follow the inverter port pinout unless you have independently verified a different cable assembly with a meter.

## Active Replacement

For active replacement, disconnect the original Solar Plug. Connect the ESPHome device through RS232 level shifting:

- ESP32 TX through RS232 adapter to inverter data input on RJ45 pin 3
- ESP32 RX through RS232 adapter from inverter data output on RJ45 pin 6
- common ground through RJ45 pin 4, with pin 5 optionally tied to the same ground

Default serial settings observed on the confirmed setup:

- `2400` baud
- `8N1`
- ASCII frames ending in carriage return (`0D`)

## Passive Sniffer

For passive sniffing, the Solar Plug remains the only master. The sniffer must be RX-only on both observed directions.
