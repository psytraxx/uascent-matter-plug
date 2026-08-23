# Original PCB Trace — WK38-V20

Notes on the donor BL0937 mains-plug board the XIAO replaces the Wi-Fi module
on. Source photos: `PXL_20260821_165630609.MACRO_FOCUS.jpg` (board back),
`PXL_20260821_165643229.MACRO_FOCUS.jpg` (module + relay wiring),
`PXL_20260821_165652054.MACRO_FOCUS.jpg` (board front), `original_chip.png` /
`original_chip_pins.png` (Uascent UAM023 module datasheet pages).

## What's visually confirmed

Board is silkscreened **WK38-V20**. Populated front side (from
`PXL_...165652054`):

- **MOV** (blue disc, marked "471K 20V") + a fusible/wirewound resistor in
  series on the L_in trace — mains surge/overcurrent protection.
- Two large electrolytic caps, both **400V 4.7µF** — HV side of a
  capacitive-drop or transformerless supply.
- **BL0937** SOIC-8 (marked "BL0937 2518 SW") — energy metering IC, per
  `docs/smart-plug-plan.md` this is the chip the new firmware's driver
  (`9f12db3`/current work) talks to.
- **FT8410A** SOIC-8, near the HV caps — unidentified from the photo alone;
  footprint and position (next to the HV caps, before the AMS1117) are
  consistent with a switching/capacitive-drop supply controller, but this is
  a guess, not a datasheet lookup.
- **AMS1117 3.3V** SOT-223 regulator (marked "AMS1117 3.3 DXG91") — steps the
  low-voltage rail down to 3.3V for the Wi-Fi/BLE module.
- Tactile pushbutton, center of board — the function button.
- Two small SMD LEDs flanking the button.
- Relay, bottom-left, 2-pole, marked "...SH-DC05V...TT-1" — switches the
  load. Coil driven from logic side; contacts carry mains.
- Castellated card-edge header, bottom-center, where the Wi-Fi/BLE
  daughtercard plugs in. What's visible in this photo is one face of the
  connector (see UAM023 pinout below for the full 11-pad picture).
- Current-sense resistor network (several 0402/0603 parts marked with values
  I couldn't resolve at this resolution, e.g. "R00_", "10R", "V10", "10A")
  clustered near the BL0937 — plausibly its CF/CF1 sampling resistors for
  voltage and current channels, but not traced to specific BL0937 pins.

Board back (`PXL_...165630609`) shows the copper for this same header
fanning out toward the BL0937/relay/button area, plus four heavy mains-rated
pads at the bottom edge (relay COM/NO and L_in/L_out) with wide copper —
consistent with the mains switching path, not signal.

The daughtercard (`PXL_...165643229`) carries a **Uascent UAM023** Wi-Fi
802.11b/g/n + BLE module (freeRTOS-based, per its datasheet), wired to the
relay coil with loose blue/red/purple leads and mounted next to two more
400V electrolytics and a second AMS1117.

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

`original_chip_pins.png` is the datasheet page for this table: it shows the
module's card-edge connector split across two rows — TOP face silkscreened
`CEN ADC P8 P7 P6` (5 pads) and BOTTOM face `3V3 GND RX1 TX1 P24 P26`
(6 pads), 11 pads total. The host board's mating footprint should be
expected to carry all 11 signals.

Which of the 11 signals the host board's driver circuitry actually *uses*
(vs. leaves as no-connects) is still **not determined from the photos** —
the traces disappear under the module footprint and I can't follow copper
across layers or under parts optically.

## Assumptions to verify (not yet confirmed)

1. The 6-pad header carries at minimum: 3.3V, GND, and a UART pair (RX1/TX1)
   — this is the obvious minimum for a smart-module interface and matches
   how the XIAO's own UART would be wired in for AT-command-style control,
   *if* that's how the original firmware talked to the module. Given this
   project instead drives GPIO directly (relay, button, LED, BL0937), the
   UART pins may be unused/no-connects on the host side.
2. Relay coil drive is a single GPIO (likely P6, P7, or P8) through a
   transistor/driver stage near the relay — not confirmed which pin, and I
   can't see a driver transistor clearly in the photos (could be integrated
   into the FT8410A, or a separate small SOT-23 hidding under solder mask
   glare).
3. Button and the two status LEDs (see below) are each on their own GPIO —
   again, which physical header pin maps to which is unknown.
4. BL0937 talks to the host over two pins (CF and CF1, its standard
   power/current pulse outputs) plus possibly SEL (voltage/current channel
   select) — whether these route to the 6-pad header directly or through
   the FT8410A is unconfirmed.
5. The FT8410A's actual identity/function is a guess based on placement, not
   verified against a datasheet.

None of these should be treated as fact until probed — they're working
hypotheses to prioritize which continuity checks to run first.

## Probing plan

Goal: map each of the 11 host-header pads to (a) UAM023 pin function, and
(b) what it connects to on the host board (BL0937 pin, relay driver,
button, LED, or 3.3V/GND).

**Safety:** do this fully USB/mains disconnected. The board should have no
power applied — you're doing continuity/resistance checks only, multimeter
in diode/continuity mode, not voltage checks on a live board.

1. **Identify and number all 11 header pads.** The card-edge connector is
   two rows: 5 pads on one face (CEN, ADC, P8, P7, P6 per the datasheet
   silkscreen) and 6 on the other (3V3, GND, RX1, TX1, P24, P26), visible in
   `PXL_...165652054` (bottom-center) and matching pads on the daughtercard
   edge in `PXL_...165643229`. Note pad order as seen from the front of the
   host board; call them H1–H11, matching the UAM023 pin numbers 1–11 from
   the datasheet table above.

2. **Find host-side 3.3V and GND first** — these are the easiest to confirm
   independently: 3.3V should show continuity to the AMS1117 output pin,
   GND should show continuity to the large ground-pour areas (e.g. relay
   mounting tab, MOV ground path, or any exposed copper on the back
   matching the pour). Probe each of H1–H11 against the AMS1117 output leg
   and against a known ground point to identify these two first.

3. **Find BL0937 connections.** With GND established, probe each remaining
   header pad against each BL0937 pin (SOIC-8, pins 1–8) for continuity.
   BL0937 pinout (standard part, for reference while probing):
   pin1 VDD, pin2 CF1, pin3 CF, pin4 SEL, pin5 IN2N, pin6 IN2P, pin7 V1P,
   pin8 GND. You're specifically looking for which header pad(s) hit CF,
   CF1, and SEL (pins 2, 3, 4) — those are the ones the driver code needs.

4. **Find the relay drive pin.** Probe each remaining header pad against the
   relay coil terminals (not the switched-contact terminals — check the
   two pins that go to the small SOT/driver transistor near the relay, or
   directly to one coil leg if driven through a simple NPN + flyback diode
   you can visually trace). If there's a visible driver transistor, probing
   its base is more informative than the coil leg itself.

5. **Find the button pin.** One tactile-switch leg should tie to GND (or
   3.3V, depending on active-high/low design); the other leg should show
   continuity to exactly one header pad — that's your button GPIO.

6. **Find both LED pins.** Two LEDs flank the button (see below) — trace
   each one's non-power leg (via its series resistor) back to a header pad
   independently; don't assume they land on adjacent pads.

7. **Record results** in a table here (pad number → net name → function),
   and cross-reference against the UAM023 datasheet pinout above to confirm
   which of pins 1–11 the host actually uses. Anything landing on RX1/TX1
   pads should prompt a check for whether those are stuffed or no-connects
   on this particular host — if unused, that's useful negative information
   too.

## Two indicator LEDs, not one

The two small SMD parts flanking the tactile button in
`PXL_20260821_165652054.MACRO_FOCUS.jpg`, each in its own distinct
footprint, are two separate status LEDs, not one. Not confirmed by
continuity/forward-voltage check, but the firmware plan
(`docs/smart-plug-plan.md`) now assigns them independent roles (relay
state, network/commissioning state) since the sealed enclosure hides the
XIAO's on-board RGB entirely — the plug's own two LEDs are the only
feedback the user ever sees. Step 6 of the probing plan above covers
finding both.

## Results

*(not yet probed — fill in once you've run the steps above)*

| Header pad | UAM023 pin (guess to confirm) | Connects to (host side) | Function |
|---|---|---|---|
| H1 | | | |
| H2 | | | |
| H3 | | | |
| H4 | | | |
| H5 | | | |
| H6 | | | |
| H7 | | | |
| H8 | | | |
| H9 | | | |
| H10 | | | |
| H11 | | | |
