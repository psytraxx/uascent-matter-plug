# Matter Smart Plug: XIAO nRF52840 Sense retrofit into a BL0937 mains plug

## Context

This repo started as a **demo app** — a Matter-over-Thread light switch. Its value was
the surrounding setup, not the application: the toolchain invocation, UF2 flash map,
USB CDC-ACM console config, and Matter stack settings are all known-good on this
board. That scaffolding is reused; the application layer has been replaced (Step A
done — the data model is now a plug server).

The actual goal: take an existing OpenBeken/BK7231N mains smart plug
and **replace its wireless module with the XIAO**, keeping the plug's PCB,
relay, tactile button, LEDs, and **BL0937** energy-metering chip. The result is a
Matter smart plug that both switches a load and reports real power/energy — replacing
the light-switch *client* role with a plug *server* role.

Decisions made with the user:

* Matter device type: **On/Off Plug-in Unit (0x010A)** + **ElectricalPowerMeasurement
  (0x0090)** + **ElectricalEnergyMeasurement (0x0091)**.
* Button: **short press = toggle relay**, **long press (3 s) = factory reset**.
* BL0937 pin mapping is **not yet traced** — the driver takes its GPIOs from
  devicetree so only the overlay changes once tracing is done.
* Deliverable includes a **wiring diagram published as an Artifact**.

---

## ⚠️ Safety constraint that shapes the whole design

The BL0937's sense side is **not galvanically isolated**. Its shunt (R001) and
voltage divider sit at mains potential, and the module's GND is tied to one side of
the mains. Therefore:

**Never connect the XIAO's USB (console/flashing) while the plug is connected to
mains.** Doing so puts your laptop's USB ground at mains potential.

This is not a warning to bolt on at the end — it dictates the bring-up order below:
all logic development happens USB-powered with **no mains present**, and metering
calibration happens only at the very end, with the console detached.

### ✅ USB-powered development with the plug board attached

**Yes — this works, and it is the recommended way to develop.** With mains
disconnected, nothing on the plug board is at a dangerous potential, so USB is safe
and the console is fully usable.

Power flows from USB → the XIAO's onboard regulator → the **3.3V-OUT** pad, which
back-feeds the plug's 3.3 V rail. That powers the BL0937, the relay driver logic, the
button, and the LEDs. All of these are testable:

| Works USB-powered | Does not |
|---|---|
| Relay driver toggling (audible click, or scope the coil drive) | Actually switching a load |
| Button short/long press | — |
| Plug LED + on-board RGB indication | — |
| Matter commissioning, OnOff, bindings | — |
| BL0937 **present and clocked**; CF/CF1/SEL wiring verifiable | Real V/I/W readings (all read zero) |

**Two precautions:**

1. **Check the relay coil voltage.** Relays in these plugs are often 5 V, driven from
   a rail the mains PSU provides — not from 3.3 V. On USB power that rail may be
   absent, so the relay may not physically click even though the drive GPIO toggles
   correctly. Verify the GPIO with a scope or meter rather than relying on the click.
   If the coil rail is derived from mains, that is expected and not a fault.
2. **Confirm the plug's PSU cannot back-feed.** Injecting 3.3 V into a rail whose
   switch-mode PSU output is unpowered is normally harmless, but confirm during Phase
   0 tracing that the 3.3 V rail has no path that would be damaged by external drive.

**Zero readings are the expected result, not a bug.** With no mains, CF/CF1 emit no
pulses. The driver's zero-power timeout (see Phase 3) should report 0 W cleanly rather
than hanging or reporting stale values — so this doubles as a **test of that timeout
path**, which is otherwise awkward to exercise.

To validate the measurement maths without mains, inject a square wave into CF/CF1 from
a signal generator or a spare nRF PWM pin (step F in the sequencing table).

---

## Strategy: prove the firmware fits before touching hardware

**Per the user: tracing is deferred.** The open question is whether the nRF52840 can
carry Matter + Thread + two measurement clusters at all. There is no point tracing a
PCB for a firmware that will not fit, so the plan front-loads a **fit check** and only
then does hardware work.

Pin assignments below are therefore **provisional** — chosen to be sensible and
electrically valid, encoded in devicetree so that the eventual trace changes only the
overlay, never code.

### The resource gate (measured, current build)

`arm-zephyr-eabi-size` on the existing light-switch image:

| Resource | Used | Available | Headroom |
|---|---|---|---|
| Flash (text+data) | 688 KB | 788 KB (`0xC5000`) | **~100 KB (87% full)** |
| RAM (bss+data) | 174 KB static | 256 KB | ~80 KB before heap/stacks |

**Flash is the binding constraint, not RAM.** The swap removes the binding handler
(163 lines) and shell, and adds ~600 lines of cluster code plus TLV encoders for the
new attribute types — encoders are usually the larger cost. Rough estimate: **20–40 KB
net growth against ~100 KB free.** Plausible, but not comfortable.

**Levers if it does not fit**, cheapest first:
1. Trim EPM optional attributes (drop RMSVoltage/RMSCurrent, keep ActivePower).
2. Lower `CONFIG_CHIP_APP_LOG_LEVEL` and Matter log levels.
3. Drop `ElectricalEnergyMeasurement`, keep power-only.
4. Reduce `CONFIG_LOG_BUFFER_SIZE` (currently 16384, sized for the boot burst).

Note lever 4 trades against the console fix in `CLAUDE.md` — shrinking it may
re-truncate the onboarding block. Prefer levers 1–3.

## Phase 0 — Deferred: trace the PCB

**Not started until the firmware is proven on the bench.** When it is:

Fill in the worksheet (`BL0937` pins 6/7/8 = CF1/CF/SEL) with a multimeter, board
unplugged and caps discharged. Beyond CF/CF1/SEL, also trace **relay driver**,
**button**, and **LED** nets, and confirm **LED polarity** (likely active-low).

Shortcut worth trying first: if the plug is a known Uascent/WK38-V20 variant, the
OpenBeken template database (`openbekeniot.github.io/webapp/devicesList.html`) may
already publish the CF/CF1/SEL/relay/button/LED roles — verify a couple with the meter
rather than trusting the table wholesale.

Record results in **`docs/plug-pinout.md`** (new file, source of truth for the overlay), with a
CONFIRMED marker and date. Also record **SEL polarity**: which SEL level routes
voltage vs current onto CF1. The driver needs this.

### XIAO pad budget

Confirmed against both `seeed_xiao_connector.dtsi` and the Seeed pinout card
(`docs/front.png`), which agree exactly:

D0=P0.02, D1=P0.03, D2=P0.28, D3=P0.29, D4=P0.04, D5=P0.05, D6=P1.11, D7=P1.12,
D8=P1.13, D9=P1.14, D10=P1.15. Power pads: **VBUS**, **GND**, **3.3V-OUT**.

The on-board RGB LED is on P0.26 (R) / P0.30 (G) / P0.06 (B) and is not on the
connector, so it stays available as a debug indicator alongside the plug's own LED.
See **Indication design** below — the two LEDs show different things.

Two constraints the card makes explicit:

* **ADC is only on D0–D5** (AIN0–AIN5). D6–D10 are digital-only. This costs nothing
  for the pulse-counting design, but means there is no analog fallback if
  pulse-counting on CF/CF1 proves unworkable — keep CF/CF1 on D0–D5 to preserve
  that option.
* **D4/D5 are the default I2C pads** and **D6/D7 the default UART pads**. Nothing in
  this design uses either bus, but avoid them if a future sensor is planned.

`&gpiote` is already enabled in `xiao_ble_common.dtsi`, which the BL0937 driver's
edge interrupts require. No Kconfig change needed for that.

### Module-internal pins (from `docs/back.png`)

The XIAO's underside carries pins that are **wired on the module but not declared in
the board devicetree** — verified absent from `xiao_ble_common.dtsi`:

| Pin | Role | Relevance here |
|---|---|---|
| P0.31 | VBAT_ADC (analog) | unused — no battery in a mains plug |
| P0.14 | READ_EN (low = enable battery read) | unused |
| P0.13 | CHG_CTRL (high = fast charge) | unused |
| P0.17 | CHG_STAT / charge LED | **free for the plug's status LED** |
| P0.09 / P0.10 | NFC1 / NFC2 | free, but need `CONFIG_NFCT_PINS_AS_GPIOS` |

**This frees up a connector pad.** P0.17 drives the module's own charge LED and is
otherwise idle in a mains application, so the plug's red LED can be driven from it
instead of D1 — leaving D1 available. Revised assignment below.

Since P0.17 is undeclared in the board devicetree, the overlay must define it as a
plain `gpio-leds` node; there is no existing alias to reuse.

**Do not repurpose P0.13/P0.14 casually.** They control the battery charger IC; with
no battery attached they are harmless, but leaving them at their reset state is the
safe default rather than driving them.

### RESET pad: fine for development, not for the enclosure

`back.png` shows **RST** broken out on the underside, and `xiao_ble_common.dtsi` sets
`&uicr { gpio-as-nreset; }` — meaning **P0.18 is configured as hardware reset**, not
as GPIO.

**Use the RST button freely during development.** The XIAO's onboard RST button and
the RST pad both work normally — press to reboot, double-tap to enter the UF2
bootloader for flashing (which is how this board is flashed anyway, per `CLAUDE.md`).
Nothing in this plan changes that, and it stays the convenient way to reset the board
while the case is open.

The single constraint concerns the **enclosed** device: once the case is closed, the
only accessible control is the plug's external tactile switch, and that switch cannot
be wired to RST — a button on RST reboots the chip before any long-press timer can
fire, making factory reset unreachable. So the external switch goes to a **GPIO**
(D0 / P0.02):

* short press → toggle relay
* long press (3 s) → Matter factory reset, wiping fabrics and reopening commissioning

Handled entirely in software by `Nrf::Board`'s `FunctionHandler`.

Because the sealed unit's only recovery path is that long press, **verification step 2
is a release gate**. If long-press
regresses, a sealed unit has no way back to commissioning short of opening it.

Note `gpio-as-nreset` means P0.18 is unavailable as a GPIO regardless; it is not on
the connector, so this costs nothing.

Six signals are needed — CF, CF1, SEL, relay, button, plug-LED — comfortably within
the 11 pads. Suggested default assignment (revise to match tracing, and prefer
keeping CF/CF1 on pads that support GPIOTE-in events; all nRF52840 pins do):

| Signal | Direction | Proposed pad |
|---|---|---|
| BL0937 CF (active power) | in, pulse | D2 / P0.28 |
| BL0937 CF1 (V or I, muxed) | in, pulse | D3 / P0.29 |
| BL0937 SEL | out | D4 / P0.04 |
| Relay drive | out | D6 / P1.11 |
| Button | in, pull-up, active-low | D0 / P0.02 |
| Plug red LED (relay state) | out | P0.17 (module-internal, see below) |

CF/CF1 are kept on D2/D3 so they retain an ADC alternate function. The relay moves to
D6 (digital-only, which is all it needs), preserving D1/D5 — and therefore AIN1/AIN3
— as spares. D5 is left free rather than D1 because D4/D5 are the I2C pair; keeping
one of them open costs nothing now and keeps a future sensor option cheap.

**Level check:** the BL0937 runs at 3.3 V and the XIAO is a 3.3 V part, so CF/CF1/SEL
are directly compatible. Confirm the plug's 3V3 rail can supply the XIAO — the
nRF52840 with Thread active draws notably more than a BK7231N in idle; measure the
PSU's headroom before committing, and note the XIAO's onboard 3.3 V regulator is
bypassed when feeding the 3V3 pad directly.

---

## Phase 1 — Data model: switch from light-switch client to plug server

This is the largest change and is mostly ZAP work, not C++.

**Regenerate the data model.** Replace `src/default_zap/light_switch.zap` with a new
`smart_plug.zap` describing endpoint 1 as On/Off Plug-in Unit with:

* `OnOff` (server) — drives the relay
* `Identify` (server) — already wired via `Nrf::Matter::IdentifyCluster`
* `ElectricalPowerMeasurement` (server)
* `ElectricalEnergyMeasurement` (server)

Use the NCS ZAP tooling (`west zap-gui` / the `zap_helpers.cmake` path already in
`CMakeLists.txt`) and regenerate `zap-generated/`. Update the
`CONFIG_NCS_SAMPLE_MATTER_ZAP_FILE_PATH` value in `prj.conf` to the new filename.

**Remove the client role.** Delete `src/light_switch.cpp` / `.h`, drop the
`binding_handler.cpp` entry from `CMakeLists.txt`, and remove the binding include
from `app_task.cpp`. `src/shell_commands.cpp` exists but `CONFIG_CHIP_LIB_SHELL=n`
means it is dead weight — delete it too, or leave it excluded from the build.

**Model reference:** `samples/matter/light_bulb` in NCS is the closest server-side
sample. Copy its `zcl_callbacks.cpp` pattern — `MatterPostAttributeChangeCallback`
dispatching on `OnOff::Id` — rather than inventing a new one.

**Product identity:** `CONFIG_CHIP_DEVICE_PRODUCT_NAME` → `"XIAO Smart Plug"` and
`CONFIG_BT_DEVICE_NAME` → `"XIAOPlug"` are done. **`CONFIG_CHIP_DEVICE_PRODUCT_ID`
is still `32772` (0x8004), the light-switch example's ID — left unchanged pending a
decision** on whether this needs its own allocation. It only matters for a real
Matter certification/distribution path; for bench use under the test vendor ID it is
cosmetic. Revisit before this goes further than one prototype.

---

## Phase 2 — Relay control and button

**New `src/relay.cpp` / `.h`** — thin GPIO wrapper over a devicetree node, following
the structure of the existing `src/status_led.cpp` (init returning errno, a `Set()`
that is safe to call repeatedly). Keep the relay's power-on default **off**, and
consider whether the plug should restore its last state across reboots — Matter's
`StartUpOnOff` attribute covers this and needs settings/NVS persistence, which is
already available since the Matter stack uses it.

**New `src/zcl_callbacks.cpp`** — implements `MatterPostAttributeChangeCallback`; on
`OnOff::Attributes::OnOff` writes to endpoint 1, calls `Relay::Set()`.

### Indication design (two LEDs, two different signals)

The plug has a **single red LED** (not RGB). Per the user, it tracks the **relay
state**, not the network state:

| Condition | Plug red LED | On-board RGB (mirror) |
|---|---|---|
| Relay ON | solid on | green solid |
| Relay OFF | off | off (or dim white) |
| Commissioning window open | **blinking** | blue blinking |
| Fatal / unrecoverable | fast blink | red solid |

This is a real change from the current code. `src/status_led.cpp` today is driven
*only* by `AppTask::UpdateStatusLed`, which reads `Nrf::GetBoard().GetDeviceState()`
— a **network** state. The plug LED's primary input is **relay** state, which comes
from a different source entirely (the OnOff attribute).

**Restructure into one indication module** — extend `src/status_led.*` (rather than
adding a parallel module) so a single owner arbitrates both LEDs and there is no risk
of two writers fighting over the same GPIO:

* Inputs: relay on/off, and `Nrf::DeviceState`
  (`DeviceDisconnected`/`AdvertisingBLE`/`ConnectedBLE`/`Provisioned`).
* **Precedence: commissioning blink outranks relay state.** While the commissioning
  window is open the LED blinks regardless of the relay, since that is the state the
  user needs to see. When commissioning completes it reverts to mirroring the relay.
* Called from two places: `UpdateStatusLed` (already registered via `Board::Init`,
  fires on every device-state change) and the new OnOff path in `zcl_callbacks.cpp`.

Keep the existing `sBlinkTimer` / `SetChannels` structure in `status_led.cpp` — it
already handles "only restart the blink timer when the state actually changes,"
which this needs. Add the plug's red LED as a fourth `gpio_dt_spec` from the overlay.

**Note the plug LED may be active-low** like the on-board ones; encode that in the
overlay's `GPIO_ACTIVE_LOW` flag (as `xiao_ble_common.dtsi` does) so the code keeps
using logical values and never inverts by hand. Confirm polarity during Phase 0.

**Button.** `AppTask::ButtonEventHandler` in `src/app_task.cpp` currently implements
the dimmer press-and-hold behavior; replace its body with a short-press handler that
toggles the OnOff attribute *through the cluster* (so the change is reported to
Matter and the local and remote state never diverge) rather than poking the relay
GPIO directly.

**Long press is already handled.** `Nrf::Board`'s `FunctionHandler` /
`mFunctionTimer` implements factory reset at
`FactoryResetConsts::kFactoryResetTriggerTimeout` = 3000 ms on button 0, with a 3 s
cancel window. No new code needed — just do not consume that button's long press in
the app handler. Note the board layer keys this to button 0, so the plug's single
physical button must be wired to the `sw0`/`button0` node.

The overlay's current two-button node (`boards/xiao_ble_nrf52840_sense.overlay`)
collapses to **one** button; the plug has a single tactile switch. Reducing
`NUMBER_OF_BUTTONS` to 1 also disables the board layer's
`AdvertisingConsts::kAdvertisingTriggerTimeout` path, which is fine.

---

## Phase 3 — BL0937 driver

**New `src/bl0937.cpp` / `.h`.** No Zephyr in-tree driver exists for this part, so
this is written from scratch. Design:

* GPIOs come from a **devicetree node in the overlay** (`cf-gpios`, `cf1-gpios`,
  `sel-gpios`), so a re-trace changes only the overlay. Use `GPIO_DT_SPEC_GET` the
  same way `status_led.cpp` does.
* **Measurement by pulse frequency.** CF frequency ∝ active power; CF1 frequency ∝
  RMS current or RMS voltage depending on SEL. Use GPIO interrupts on both edges (or
  one edge) with a hardware timestamp; count pulses over a fixed window (~1 s) rather
  than measuring individual periods, which is far more robust at low power where
  pulses are seconds apart.
* **SEL multiplexing.** Alternate SEL every measurement window to sample voltage and
  current in turn. Discard the first window after each SEL flip — the BL0937 needs
  settling time and the first reading straddles both modes. This gives V and I at
  roughly 0.5 Hz each, which is ample for Matter reporting.
* **Zero-power handling.** With no load, CF stops pulsing entirely. Implement a
  timeout (e.g. 3 s without a pulse ⇒ report 0 W) or the driver will hold the last
  nonzero reading forever.
* **Calibration constants** in a header, derived from the plug's actual shunt and
  divider values, with a Kconfig or `plug-pinout.md`-documented override. Compare against
  a known load to fit them.

**Energy accumulation.** Integrate active power over time for
`ElectricalEnergyMeasurement`'s cumulative import. Persist the accumulator
periodically (Zephyr settings / NVS) so a reboot does not zero the meter — but write
infrequently (e.g. every few minutes, or on significant delta) to protect flash
endurance.

---

## Phase 4 — Matter measurement clusters

**New `src/power_measurement.cpp` / `.h`.**

* Implement `chip::app::Clusters::ElectricalPowerMeasurement::Delegate` (see
  `modules/lib/matter/src/app/clusters/electrical-power-measurement-server/electrical-power-measurement-server.h`).
  It is a large pure-virtual interface — most methods return
  `CHIP_ERROR_NOT_IMPLEMENTED` or an empty `Nullable`. Implement meaningfully:
  `GetActivePower`, `GetRMSVoltage`, `GetRMSCurrent`, `GetPowerMode` (AC),
  `GetNumberOfMeasurementTypes`, and the accuracy-list methods.
* Construct an `ElectricalPowerMeasurement::Instance` with the
  `kOptionalAttributeRMSVoltage | kOptionalAttributeRMSCurrent` optional-attribute
  bits set, and call `Init()` from the `mPostServerInitClbk` in `AppTask::Init()` —
  the same hook that currently calls `LightSwitch::Init`.
* **Units matter.** EPM attributes are `int64_t` in **mW / mV / mA**. Getting the
  scaling wrong is the most likely source of nonsense readings in a controller.

**Confirmed from the ZCL XML** (`zcl/data-model/chip/electrical-power-measurement-cluster.xml`,
`global-structs.xml`) — worth having to hand when writing the delegate:

| Attribute | Code | Type | Optional? |
|---|---|---|---|
| `PowerMode` | 0x0000 | `PowerModeEnum` | mandatory — **AC = 0x02** |
| `NumberOfMeasurementTypes` | 0x0001 | `int8u`, min 1 | mandatory |
| `Accuracy` | 0x0002 | `MeasurementAccuracyStruct[]` | mandatory, min 1 entry |
| `ActivePower` | 0x0008 | `power_mw` | **mandatory** |
| `RMSVoltage` | 0x000B | `voltage_mv` | optional |
| `RMSCurrent` | 0x000C | `amperage_ma` | optional |

So `ActivePower` needs no optional-attribute bit; only RMSVoltage/RMSCurrent do. All
are `isNullable`, so "no reading yet" is a null rather than a zero — use that for the
pre-first-sample state and reserve 0 mW for a genuinely measured zero.

`MeasurementAccuracyStruct` = { MeasurementType, Measured (bool), MinMeasuredValue,
MaxMeasuredValue, AccuracyRanges[] (min 1 entry) }.
* For energy, call the free function
  `ElectricalEnergyMeasurement::NotifyCumulativeEnergyMeasured(...)` with an
  `EnergyMeasurementStruct` — energy is in **mWh**. Also
  `SetMeasurementAccuracy(...)` once at init.
* **Reporting cadence.** Do not report every sample. Apply a deadband (report on a
  meaningful delta, plus a periodic heartbeat) — a Thread network and a sleepy
  controller should not see 1 Hz attribute writes.

---

## Phase 5 — Build, flash budget, verification

**Flash is the main risk** — measured numbers and mitigation levers are in *The
resource gate* above; the sequencing table drives when they get checked (steps B and
D). `CONFIG_LTO=y` stays mandatory.

Build with the memory-constrained job pool per CLAUDE.md:

```
west build -b xiao_ble/nrf52840/sense -- -DCMAKE_JOB_POOLS="compile=4;link=1"
```

### Verification, in a safe order

1. **Bench, no mains, USB powered.** Confirm it builds, boots, commissions, and the
   status LED behaves. Toggle OnOff from a controller (`chip-tool` or Home Assistant)
   and verify the relay GPIO toggles — measure with a meter or scope on the pad,
   relay not yet connected.
2. **Bench, button and LEDs.** Short press toggles OnOff and the controller sees the
   reported change. Long press (3 s) factory resets and reopens commissioning.
   **Treat this as a release gate, not a checkbox** — in the sealed enclosure the
   long press is the only recovery path, so a regression here strands the unit.
   Verify LED precedence explicitly: with the relay ON, open the commissioning
   window and confirm the plug LED switches from solid to blinking, then reverts to
   solid once commissioned.
3. **Soldered into the plug, USB powered, no mains.** The main functional test — see
   *USB-powered development* above. Confirm relay GPIO drive (scope the pad; the coil
   may not click if it needs a mains-derived rail), button, both LEDs, and
   commissioning. Confirm the meter reports a clean **0 W** rather than stale or
   garbage values, which exercises the zero-power timeout path.
4. **BL0937 measurement chain, still no mains.** Add a bring-up path that logs raw
   CF/CF1 frequencies and SEL state. Inject a square wave into CF/CF1 from a signal
   generator (or a spare nRF PWM pin) at a known frequency and confirm the driver
   computes the expected value. This validates the entire measurement chain with zero
   mains exposure — only the calibration constants remain unverified.
5. **Mains, isolated, console detached.** Only after 1–4 pass. Use an isolation
   transformer if available. With USB **disconnected**, energize with a known
   resistive load (an incandescent bulb or heater of known wattage) and read the
   power over Thread from the controller. Calibrate the constants against that load.
6. **Regression.** Verify factory reset still works after metering is active, and
   that the energy accumulator survives a reboot.

---

## Deliverables

* `plug-pinout.md` — traced net list, confirmed pin assignments, SEL polarity,
  calibration constants.
* Updated `boards/xiao_ble_nrf52840_sense.overlay` — single button, relay, plug red
  LED on P0.17, BL0937 node.
* New `src/relay.*`, `src/bl0937.*`, `src/power_measurement.*`, `src/zcl_callbacks.cpp`.
* `src/meter_stub.*` — synthetic meter behind a Kconfig option, used for the step-C
  fit check and retained for mains-free testing of the Matter reporting path.
* Extended `src/status_led.*` — arbitrates plug red LED (relay state, commissioning
  blink takes precedence) and mirrors it in colour on the on-board RGB.
* Rewritten `src/app_task.cpp`; deleted `src/light_switch.*`, `src/shell_commands.*`.
* New `src/default_zap/smart_plug.zap` + regenerated `zap-generated/`.
* Updated `prj.conf` (product name/ID, BT name, ZAP path) and `CMakeLists.txt`.
* **Wiring diagram published as an Artifact** — XIAO pads ↔ plug PCB nets, with the
  isolation warning and the double-tap-RESET UF2 flashing procedure.
* Updated `CLAUDE.md` reflecting the plug role, the new pin map, and the mains-safety
  rule.

## Review findings (post Step A)

A multi-angle review of the Step A branch. Two real bugs found and fixed; the
rest are recorded so they are not rediscovered.

**Fixed:**

* **LEDs stayed dark at boot.** `sNetworkState` was initialised to `Error`,
  and the board layer's one unconditional `Board::Init()` callback also passes
  `Error` (its `mState` defaults to `DeviceDisconnected`) — so the
  "unchanged" guard swallowed the first render and nothing lit up. Worse than
  it sounds, because `Board::UpdateDeviceState()` has its own `!=` guard, so
  the LEDs would have stayed dark until commissioning began. Fixed with an
  explicit `sRendered` flag.
* **Dead button path.** `APPLICATION_BUTTON_MASK` was `DK_BTN2_MSK`, but the
  overlay now declares a single button, so bit 1 could never be set — the
  whole handler branch was unreachable. Now `DK_BTN1_MSK`, matching the board
  layer's function button. The dimmer timers/handlers were deleted outright
  rather than left as stubs; git history has them if needed.

**Known and deliberate:**

* `RelaySet()` has no callers. OnOff commands from a controller return
  Success and update the attribute but do not switch the relay — the
  `MatterPostAttributeChangeCallback` bridge is Phase 2. **This will look like
  working On/Off in a controller UI**, so do not treat that as a passing test.

**Watch items:**

* The overlay's `gpio-leds`/`gpio-keys` reuse is safe only because
  `CONFIG_LED` and `CONFIG_INPUT` are both off (verified in the build's
  `.config`). If anything turns either on, those drivers would attach to
  `relay_drive`/`bl0937_*` and fight the application for the pins — a driver
  could drive the relay at init, before `RelayInit()` runs.
* `Apply()` and `BlinkTimerHandler()` each spell out the state→channel
  mapping. A third indication axis (identify, OTA) would multiply the cases;
  worth collapsing to one render function at that point, not before.

## Sequencing — fit check first, hardware last

Ordered so the **resource question is answered as early and as cheaply as possible**,
since that is the user's gate on the whole project. No PCB tracing, no soldering, and
no mains until the firmware is proven on the bench.

| Step | Work | Gate |
|---|---|---|
| **A** | Phase 1 data model swap (ZAP → plug server), remove light-switch/binding/shell | builds clean |
| **B** | **Measure flash + RAM.** `arm-zephyr-eabi-size build/blink/zephyr/zephyr.elf` | headroom still positive |
| **C** | Phase 4 clusters wired to a **fake meter** — a stub emitting a synthetic sine/ramp instead of BL0937 data | **the real gate:** full data model on the device, controller reads plausible W/V/A |
| **D** | **Re-measure.** This is the true worst case — all clusters and TLV encoders linked in | fits with margin |
| **E** | Phase 2 relay + button + LED indication, on bare XIAO with an LED on the relay pad | short/long press verified |
| **F** | Phase 0 tracing → fill `plug-pinout.md` → update overlay | pins CONFIRMED |
| **G** | **Solder into plug, power via USB, no mains.** Full functional test: relay drive, button, LEDs, commissioning | everything but real readings works |
| **H** | Phase 3 BL0937 driver, validated by injecting a square wave into CF/CF1 | computed values match injected frequency |
| **I** | Mains bring-up, console detached | calibrated against known load |

**Step C is the decision point.** A stub meter exercises every expensive thing — both
clusters, their attribute storage, the TLV encoders, the reporting engine — while
costing an afternoon and zero hardware. If it does not fit there, it will not fit with
a real driver, and the levers listed above get pulled before any hardware work.

The fake-meter stub also earns its keep afterwards: it stays as a build option for
testing the Matter side without mains, which is the only safe way to iterate on
reporting behaviour.

Steps A–E need **no PCB access at all** — the firmware risk is retired on a bare XIAO,
which is the ordering the user asked for.

**Mains appears only at step I.** Steps G and H run with the XIAO soldered into the
plug but powered from USB, which is safe (no mains present) and gives full console
access. That covers essentially all the functionality: only the real V/I/W readings
need mains, and even the measurement maths is validated by signal injection at step H.

Practically, this means **the console is available for the entire development cycle**,
and the only step performed blind is the final calibration.
