# Original PCB Trace — WK38-V20

Notes on the donor BL0937 mains-plug board the XIAO replaces the Wi-Fi module
on. Source photos: `PXL_20260821_165630609.MACRO_FOCUS.jpg` (board back),
`PXL_20260821_165643229.MACRO_FOCUS.jpg` (module + relay wiring),
`PXL_20260821_165652054.MACRO_FOCUS.jpg` (board front), `original_chip.png` /
`original_chip_pins.png` (Uascent UAM023 module datasheet pages), `bl0937.png`
(BL0937 pinout).

## Donor device identity

- **Product:** Uascent UA-SmartPlug (Matter Wi-Fi smart plug).
- **Matter certification (stock firmware):** Vendor ID **0x1400**, Product ID
  **0x03EA**, device type **0x010A** (On/Off Plug-in Unit), Spec 1.3,
  transport Wi-Fi + BLE. Certified firmware v2.3, hardware v1.0.
- **FCC ID:** **2A68EJX-UAM023** (Shenzhen Uascent Technology, filed 2023-03).
  The FCC "Internal Photos" exhibit shows the de-shielded UAM023 module.
- **Stock cloud endpoints:** the original firmware kept on/off local over
  Matter but uploaded metering to the U-Home / Xthings cloud —
  `api.u-tec.com` (API) and `oauth.u-tec.com` (auth). Cloud-only metering is
  the thing this project replaces; these names are worth having when
  confirming the finished plug makes no outbound connections.

## Board layout

Board is silkscreened **WK38-V20**. Populated front side (from
`PXL_...165652054`):

- **MOV** (blue disc, marked "471K 20V") + a fusible/wirewound resistor in
  series on the L_in trace — mains surge/overcurrent protection.
- Two large electrolytic caps, both **400V 4.7µF** — HV side of a
  capacitive-drop or transformerless supply.
- **BL0937** SOIC-8 (marked "BL0937 2518 SW") — energy metering IC that the
  firmware driver talks to (see `docs/smart-plug-plan.md`).
- **FT8410A** SOIC-8, near the HV caps — footprint and position (next to the
  HV caps, before the AMS1117) are consistent with a switching/capacitive-drop
  supply controller.
- **AMS1117 3.3V** SOT-223 regulator (marked "AMS1117 3.3 DXG91") — steps the
  low-voltage rail down to 3.3V for the Wi-Fi/BLE module.
- Tactile pushbutton, center of board — the function button.
- Two small SMD LEDs flanking the button — two independent status LEDs, not
  one (distinct footprints).
- Relay, bottom-left, 2-pole, marked "...SH-DC05V...TT-1" — switches the
  load. Coil driven from logic side; contacts carry mains.
- Castellated card-edge header, bottom-center, where the Wi-Fi/BLE
  daughtercard plugs in (11 pads — see UAM023 pinout below).
- Current-sense resistor network near the BL0937 (several 0402/0603 parts),
  plausibly its CF/CF1 sampling resistors for voltage and current channels.

Board back (`PXL_...165630609`) shows the copper for this header fanning out
toward the BL0937/relay/button area, plus four heavy mains-rated pads at the
bottom edge (relay COM/NO and L_in/L_out).

The daughtercard (`PXL_...165643229`) carries a **Uascent UAM023** Wi-Fi
802.11b/g/n + BLE module (freeRTOS-based), wired to the relay coil with
loose blue/red/purple leads and mounted next to two more 400V electrolytics
and a second AMS1117.

## UAM023 module pinout (from datasheet, `original_chip_pins.png`)

| Pin | Name | Function          |
|-----|------|--------------------|
| 1   | 3.3V | Power in           |
| 2   | P6   | GPIO6/PWM0         |
| 3   | GND  | Ground             |
| 4   | P7   | GPIO7/PWM1         |
| 5   | RX1  | UART RX1 / GPIO10  |
| 6   | P8   | GPIO8/PWM2         |
| 7   | TX1  | UART TX1 / GPIO11  |
| 8   | ADC  | ADC / GPIO23       |
| 9   | P24  | GPIO24/PWM4        |
| 10  | CEN  | Reset              |
| 11  | P26  | GPIO26/PWM5        |

The module's card-edge connector is two rows: TOP face silkscreened
`CEN ADC P8 P7 P6` (5 pads) and BOTTOM face `3V3 GND RX1 TX1 P24 P26`
(6 pads), 11 pads total.

## BL0937 pinout (from `docs/bl0937.png`)

| pin1 | pin2 | pin3 | pin4 | pin5 | pin6 | pin7 | pin8 |
|---|---|---|---|---|---|---|---|
| VDD | IP | IN | VP | GND | CF | CF1 | SEL |

CF = active-power pulse output, CF1 = V/I pulse output (muxed by SEL), SEL =
V/I channel select.

## Probing plan

Goal: map each of the 11 host-header pads to (a) UAM023 pin function, and
(b) what it connects to on the host board (BL0937 pin, relay driver,
button, LED, or 3.3V/GND).

**Safety:** do this fully USB/mains disconnected. The board should have no
power applied — continuity/resistance checks only, multimeter in
diode/continuity mode, not voltage checks on a live board.

Remaining steps:

1. **3.3V and GND** — probe each header pad against the AMS1117 output leg
   and against a known ground point (e.g. relay mounting tab, MOV ground
   path).
2. **Relay drive** — probe remaining header pads against the relay coil
   terminals or driver-transistor base near the relay.
3. **Both LEDs** — trace each LED's non-power leg (via its series resistor)
   back to a header pad independently; don't assume they land on adjacent
   pads. Confirm polarity for each independently — they may not match.

## Results

First batch, from continuity probing.

| Header pad | UAM023 pin | Connects to (host side) | Function |
|---|---|---|---|
| H1 | 3.3V | AMS1117 output | Power in |
| H3 | GND | Ground pour | Ground |
| H5 | RX1 | Tactile button | Button input |
| H7 | TX1 | *(not connected)* | No-connect |
| H9 | P24 | BL0937 pin 6 | CF (active-power pulse) |
| H11 | P26 | BL0937 pin 7 | CF1 (V/I pulse, muxed by SEL) |
| H6 | P8 | BL0937 pin 8 | SEL (V/I select) |
| H2 | P6 | LED 1 | Status LED 1 |
| H4 | | | |
| H8 | | | |
| H10 | | | |

Still to probe: relay driver, LED 2, and polarity for both LEDs.
