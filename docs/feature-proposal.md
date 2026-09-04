# Proposal — features the stock firmware had that we don't

The stock Uascent firmware carried four configurable behaviours plus a
reporting policy that this project has no equivalent for. This is a proposal
for which of them to reimplement, with reasoning; see
`docs/original-firmware.md` for how each worked originally.

The guiding question is: **does this need to be local?** The stock plug's
automation features existed because its cloud was the only controller. A
Matter plug lives on a network with a real controller (Home Assistant, Apple
Home, Google) that already does scheduling and conditional logic, and does it
better. So the features worth keeping locally are the ones that either protect
the hardware or must work when no controller is reachable.

Recommendation summary:

| Feature | Recommendation |
|---|---|
| Over-power protection | **Implement** — safety, must be local |
| Attribute-report deadbands | **Implement** — fixes a real responsiveness gap |
| Inching (auto-off) | Defer — no clean Matter surface |
| Energy budget ("saving mode") | Skip — controller's job |
| Away mode (schedule) | Skip — controller's job |

---

## 1. Over-power protection — recommend implementing

The only feature here that genuinely must be local. A 10 A relay welded shut
by a sustained overload is a fire risk, and waiting for a controller to notice
is the wrong architecture.

**How the original did it.** Each 1 s tick compared the *raw* per-second power
sample — before the median filter — against a threshold, and required **more
than four consecutive breaches** before acting, then forced the relay off. The
debounce is what makes it safe to set the threshold near the rating: motor and
PSU inrush lasts well under 5 s, so it cannot nuisance-trip.

**Proposed implementation.**

- Check the raw counts-per-second sample in `MeterPoll()`, before
  `sCfFilter.Push()`, mirroring the original's placement.
- Trip after 5 consecutive samples over threshold (>4, as the original).
- Default threshold **2400 W** — just above this plug's 10 A / ~2300 W
  ceiling. Make it a Kconfig, not an NV key, until there is a reason to
  change it at runtime.
- On trip: force the relay off, latch, and log at `LOG_ERR`.
- **Clearing the latch** is the design decision the original left vague. It
  stored a `pretect action` byte, implying a configurable response, but the
  path we recovered simply turns the relay off. Proposed: the latch clears on
  an explicit On command or a button press, never automatically — an
  auto-clearing overload trip just re-energises a fault.

**Matter surfacing.** There is no standard cluster for an overload trip in
Matter 1.3. The controller sees `OnOff` go false, which is honest but
uninformative. Options, in order of preference:

1. Local-only: relay off, logged. Controller sees the plug turn off.
2. Add the `BooleanState` cluster on a second endpoint as a fault flag.
3. Manufacturer-specific attribute — most informative, least portable.

Recommend starting with (1); it is a handful of lines and carries the whole
safety benefit. (2) can follow if the state needs to be visible.

**Caveat to resolve first.** The trip threshold is only as trustworthy as the
calibration, and the calibration has not yet been checked against a reference
meter. Protection should land *after* mains bring-up validates the power
reading, not before — a miscalibrated threshold is worse than none.

## 2. Attribute-report deadbands — recommend implementing

This one fixes a gap we have independently of the stock firmware.

**The gap.** `PlugPowerDelegate::Set()` stores values; nothing calls
`MatterReportingAttributeChangeCallback()`. So a Matter subscriber does not
learn about a change until its own max interval elapses — a plug that jumps
from 0 W to 2000 W may take tens of seconds to show it.

**How the original did it.** It reported on change, not on a timer, gated by
fixed deadbands: it computed `fabsf(current - last_reported)` and pushed only
when the delta exceeded roughly 0.05 and 0.003 for two of the quantities.

**Proposed implementation.** Call the reporting callback from
`PowerMeasurementUpdate()` when a value moves more than its deadband since the
value last reported:

| Attribute | Proposed deadband |
|---|---|
| ActivePower | 50 mW |
| RMSCurrent | 3 mA |
| RMSVoltage | 100 mV |

Power and current mirror the original's 0.05 / 0.003. Which stock quantity
each of those two constants belonged to is not fully pinned down, and the
voltage figure has no stock counterpart, so treat all three as tunable
starting points rather than recovered values.

Note these deadbands are far finer than the hardware resolution — one CF pulse
per second is ~1.29 W — so in practice every genuine change reports and the
deadband only suppresses arithmetic jitter. That is the intent: bound the
traffic without adding latency.

## 3. Inching / auto-off — recommend deferring

Turn off automatically N seconds after being turned on. Genuinely useful, but
there is no clean way to *configure* it over Matter.

The On/Off cluster's `OnTime` / `OffWaitTime` attributes and `OnWithTimedOff`
command are exactly this behaviour, but they belong to the cluster's `LT`
(Lighting) feature, which is not part of the On/Off Plug-in Unit (0x010A)
device type. Claiming it would be non-conformant.

That leaves a manufacturer-specific cluster, which no off-the-shelf controller
will drive. Meanwhile every controller can already express "turn off 30 s
after it turns on" as an automation.

Recommend skipping unless a concrete use case appears that must survive
controller loss.

## 4. Energy budget ("saving mode") — recommend skipping

The original accumulated energy and cut power when it exceeded a stored
budget. Reimplementing it means answering questions the stock firmware never
clearly did: over what period does the budget run, when does it reset, does it
survive reboot?

A controller with the `ElectricalEnergyMeasurement` attribute we already
publish can express this in one automation, with a reset policy the user
actually chooses. Recommend leaving it there.

## 5. Away mode — recommend skipping

A schedule window driven by a 17-byte time string. Pure scheduling, needs a
real clock, and is squarely the controller's job. Nothing is gained by doing
it on the plug.

---

## Suggested order

1. Mains bring-up and calibration check (already Phase 3 in the plan) —
   everything else depends on the power reading being trustworthy.
2. Attribute-report deadbands — small, independent of calibration accuracy,
   and fixes subscriber responsiveness today.
3. Over-power protection — once the calibration is confirmed.
