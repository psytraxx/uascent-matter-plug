# Second Donor Board — CB2S/BK7231N Module

Notes on a second BL0937 mains-plug board, originally shipping a Tuya
**CB2S** Wi-Fi module (BK7231N SoC, same chip family as the Uascent UAM023
covered in `docs/original-pcb-trace.md`, but a different vendor/module and a
different stock firmware).

**This board reuses the same XIAO wiring as the default unit** —
`boards/xiao_ble.overlay` is not changed for it. See CLAUDE.md, "Hardware
variants (pairing code only, not GPIO)", for why: the XIAO is hand-soldered
per plug, so whichever physical pad this board's own copper routes a signal
to just gets wired to the same fixed D0–D5 pin the default unit uses. Only
this board's own pad *identities* need tracing, not a new overlay.

## Provenance

Extracted from a Tuya flash dump: the Tuya config section starts at the usual
offset `2023424` (`0x1EE000`). `em_sys_env` in the dump reports `"bk7231n"`,
confirming the SoC. Config below is `user_param_key` from that dump —
Tuya's own GPIO/behaviour assignment table, read by the stock firmware at
boot, not something inferred or guessed.

```json
{
  "sel_pin_pin": 24,
  "rl1_lv": 1,
  "bt1_pin": 10,
  "net_trig": 2,
  "netled1_lv": 0,
  "netled_reuse": 1,
  "bt1_type": 0,
  "vi_pin": 6,
  "resistor": 1,
  "over_cur": 17000,
  "bt1_lv": 0,
  "reset_t": 3,
  "netled1_pin": 8,
  "chip_type": 0,
  "lose_vol": 80,
  "over_vol": 280,
  "module": "CB2S",
  "ele_pin": 7,
  "ch1_stat": 2,
  "rl1_type": 0,
  "ch_num": 1,
  "ele_fun_en": 1,
  "rl1_pin": 26,
  "vol_def": 0,
  "sel_pin_lv": 1,
  "crc": 7
}
```

## GPIO map, as configured by Tuya

These `P<n>` numbers are BK7231N SoC GPIO numbers — the same naming
convention `docs/original-pcb-trace.md` uses for the UAM023, since it's the
same SoC family. **They are Tuya's/CB2S's own pin assignment, independent of
and different from the UAM023's** (compare CF=P24/CF1=P26/SEL=P8/relay=P6 on
the UAM023 against the table below) — two vendors picked different GPIOs on
the same chip for the same job, which is exactly what you'd expect and is
why nothing here can be assumed to transfer between boards.

| Config key | Value | Signal | Notes |
|---|---|---|---|
| `sel_pin_pin` | P24 | BL0937 SEL (V/I select) | `sel_pin_lv: 1` — TODO: confirm polarity like the UAM023's SEL-high-selects-voltage finding; don't assume it matches |
| `bt1_pin` | P10 | Button (channel 1) | `bt1_lv: 0`, `bt1_type: 0` |
| `vi_pin` | P6 | BL0937 CF1 (V/I pulse, muxed by SEL) | |
| `netled1_pin` | P8 | Network/status LED | `netled1_lv: 0`, `netled_reuse: 1` |
| `ele_pin` | P7 | BL0937 CF (active-power pulse) | `ele_fun_en: 1` |
| `rl1_pin` | P26 | Relay (channel 1) | `rl1_lv: 1`, `rl1_type: 0` |

Also present: `over_cur: 17000`, `over_vol: 280`, `lose_vol: 80` — this
board's own over-current/over-voltage/brownout thresholds. Units aren't
confirmed from the config alone (compare against the `000004fhhe` DPID table
in the same dump, id 17/18/19, before reusing these as calibration or
`APP_OVERPOWER_THRESHOLD_MW` inputs) — don't carry them over without
checking against a real load, same as the UAM023's calibration constants
were.

## Still to determine

Exactly what `docs/original-pcb-trace.md` had to determine for the UAM023,
and for the same reason — Tuya's GPIO assignment says what the *module*
did, not which *physical solder pad* on this board's own PCB carries each
signal once the module is desoldered:

1. **Physical pad tracing.** Continuity-probe this board's own module
   footprint/header (fully USB/mains disconnected) to find which pad each
   `P<n>` above lands on, the same way `docs/original-pcb-trace.md` built
   its "Header pad mapping" table. CB2S's own pinout (pad position ↔ GPIO
   name) is a separate lookup from this Tuya config and hasn't been
   confirmed against this specific board yet — don't assume a generic CB2S
   pinout diagram matches this PCB's copper without tracing it, the same
   caution `docs/original-pcb-trace.md` ends on for the UAM023.
2. **LED and button polarity**, and whether this board's LED sits on the
   relay-drive net the way the UAM023's does (`rl1_lv`/`netled1_lv` above
   are Tuya's logic-level config, not yet confirmed against the bare board).
3. **Calibration constants** — this unit's own `bl0937`-equivalent NV
   values, wherever Tuya stores them in this dump, still need pulling and
   checking against a real load before trusting any readings.
4. Whether the relay is *actually* P26 or, as happened with the UAM023
   (P6 initially misread from continuity probing as an LED), the firmware
   disassembly needs to arbitrate a discrepancy. No such discrepancy is
   known yet here because no continuity probing has been done on this board.

Once traced, wire this board's physical pads to XIAO pins D0 (button), D1
(network LED), D2 (CF), D3 (CF1), D4 (SEL), D5 (relay) — the same assignment
`boards/xiao_ble.overlay` already uses for the first unit. Give this unit its
own pairing code via `boards/xiao_ble_v2.conf` (`VARIANT=v2`) rather than
reusing the default unit's discriminator.
