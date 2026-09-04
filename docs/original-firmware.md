# Original Firmware Behaviour — Uascent UAM023

How the stock firmware actually worked, recovered by disassembling the
decrypted flash dump in `temp/`. Companion to `docs/original-pcb-trace.md`,
which covers the board and pin map; this file covers *behaviour*.

## Method

The dump was decrypted with the OpenBeken flash reader (it **was** encrypted —
the raw image is scrambled, the decrypted one starts with a valid ARM vector
table). Analysis notes, so the work can be repeated:

- Load base is **`0x10000`**; the app is **Thumb** code. Confirmed by resolving
  string pointers to real literal pools.
- Pin and config data is **not** immediates. It lives in a `.data` image copied
  at boot from flash VA `0x122858` to RAM `0x400100` (copy loop at `0x10230`).
- NV storage namespace is **`BEKEN0`**; `nv_read(ns, key, dst, len, &outlen)`
  is at `0x1b870`, `nv_write` at `0x1b85c`.
- GPIO helpers: `gpio_config(desc)` `0x1b718`, `gpio_output(desc, val)`
  `0x1b780`, `gpio_irq_attach` `0x1b7ac`. Descriptors are 2-byte
  `{gpio, mode}`.

## Architecture

Beken **Armino SDK** (FreeRTOS) + **connectedhomeip (Matter 1.2)**, with a
vendor application layer branded **"JX"**. The module identifies itself as
`matter-1.2/UAM023-ARM-C0`.

Device identity, from a JSON blob compiled into the image:

```json
{"baseInfo":{"softVersion":"1.10","hardVersion":"1.0","mqtt":1,
 "vendorId":5120,"typeId":"0x010A","vendorName":"Uascent","mpid":1002},
 "clusterDPs":[{"clusterId":6,"dpId":1,...},{"clusterId":6,"dpId":8,...},
               {"clusterId":6,"dpId":17,...}]}
```

`typeId 0x010A` is On/Off Plug-in Unit — the same device type this project
targets. Note `"mqtt":1` and the Tuya-style `clusterDPs` mapping: the stock
firmware bridged Matter cluster 6 (On/Off) to cloud "datapoints" alongside
serving Matter proper.

## Task and timer model

The metering subsystem is set up by one init function at `0x13fa0`, which
reads every NV key, then starts:

| Object | Handle | Period / prio | Callback | Job |
|---|---|---|---|---|
| `"irq timer"` | `0x4027ac` | 1000 ms, repeating | `0x13a78` | pulse → power/V/I conversion |
| `"cur timer"` | `0x4027ac` | 1000 ms, repeating | `0x13554` | energy accumulation, change reporting |
| `"elec task"` | `0x4027b8` | 7168 B stack, prio 4 | `0x13858` | batches energy records, uploads to cloud |

Both timers run at **1 Hz**. Confirmed at `0x14164` and `0x14056`
(`movs r2,#0xfa; lsls r2,r2,#2` = 1000 ms).

## Metering pipeline

This is the part worth copying. The BL0937 driver is `0x13a78`, run once per
second.

### 1. Pulse capture

CF and CF1 each have a bare counter ISR — the only two interrupt handlers in
the firmware that just increment:

```
134f8: CF  ISR   ldr r2,=0x402774 ; ldr r3,[r2] ; adds r3,#1 ; str r3,[r2]
13508: CF1 ISR   ldr r2,=0x40276c ; ldr r3,[r2] ; adds r3,#1 ; str r3,[r2]
```

Both counters are **zeroed at the end of every 1 s tick** (`0x13b52`), so each
sample is a raw counts-per-second frequency.

### 2. Median-of-3 filter

Each channel keeps a 3-slot ring (`0x402788` for CF, `0x40277c` for CF1). On
every third tick the three samples are sorted (bubble sort at `0x13a2c`) and
the **middle one** taken. So a fresh reading is produced **every 3 seconds**,
and single-tick glitches are rejected outright.

### 3. Scaling

The median frequency is divided by a calibration constant. The constants live
in a struct at `0x402760`, loaded from the NV key `bl0937`:

| Field | Value (this unit) | Divides | Yields |
|---|---|---|---|
| `calib[0x0]` | 8.0772724 | CF1 while SEL **high** | volts |
| `calib[0x4]` | 91.6363602 | CF1 while SEL **low** | amps |
| `calib[0x8]` | 0.7752066 | CF | watts |

```
power_W   = median(CF)  / 0.7752066     ; 0x13ad0-0x13ad8 -> accum[0x0]
voltage_V = median(CF1) / 8.0772724     ; 0x13b1e-0x13b26 -> accum[0x4]
current_A = median(CF1) / 91.6363602    ; 0x13b8c-0x13b94 -> accum[0x8]
```

Results land in the meter struct at `0x402794`. These are *divisors* —
"counts per second per unit" — which is the same convention `src/bl0937.cpp`
already uses.

### 4. SEL multiplexing — polarity

**SEL high selects voltage; SEL low selects current.** The firmware is
unambiguous about this, and it is the opposite of what `src/bl0937.cpp`
currently assumes.

At `0x13b06` the code branches on the SEL level that was in effect during the
window just measured (stashed at `sp[0]`):

```
13b06: cmp  r1, #0            ; r1 = SEL level during this window
13b08: beq  0x13b78           ; SEL == 0 -> current path
       ...                    ; SEL != 0 -> falls through to voltage path
13b1e: ldr  r1, [r3]          ;   calib[0x0] = 8.0772  -> volts   -> accum[0x4]
13b8c: ldr  r1, [r3, #0x4]    ;   calib[0x4] = 91.636  -> amps    -> accum[0x8]
```

Corroborating detail: the `.data` image sets the initial SEL level
(`0x40010b`) to **1**, i.e. the firmware boots into the voltage phase.

SEL is toggled every **3 seconds**, matching the 3-sample filter window, by an
accumulator compared against 2999 ms:

```
13b2c: r3 = elapsed + 1000 ; cmp r3, #2999 ; ble skip
13b42: rsbs r3,r1,#0 ; adcs r1,r3   ; r1 = !r1  (logical NOT)
13b48: strb r1,[0x40010b]           ; save new level
13b4a: bl gpio_output(SEL, r1)      ; drive P8
```

Because the sample is attributed to the level that was *active during* the
window rather than the level just written, there is no settling-window
discard — the firmware simply never mislabels a window.

## Energy accumulation and reporting

The `"cur timer"` (`0x13554`) runs at 1 Hz and does two things.

**Energy integration.** Power is integrated to watt-hours using the constant
`3600000.0` (ms per hour) with a `1000.0` scaling divisor — the standard
`Wh += W * dt_ms / 3600000` accumulation, in double precision.

**Change-triggered reporting.** Rather than reporting on a timer, it reports
on change. It computes `fabsf(current - last_reported)` (the sign bit is
cleared with `lsls #1 / lsrs #1` at `0x1362c`) and compares against fixed
deadbands:

| Quantity | Deadband |
|---|---|
| power | 0.05 |
| a second channel | 0.003 |
| accumulator reset thresholds | 0.01 and 0.02 |

Only when a reading moves by more than its deadband is it pushed out. That is
worth imitating — it keeps Matter subscription traffic down without adding
latency to real changes.

The `"elec task"` thread then batches records for the cloud, building a
payload with `beginTime` / `endTime` / `mac` / `spid` / `dataArray` /
`powerConsumption` under a `device_status_notify` message type. Failures log
`!!!report fail, code:%d`.

## Automation and protection features

Four configurable behaviours, each stored as a mode byte plus a value, read at
boot and installed by its own setter.

| Feature | NV keys | Setter | State |
|---|---|---|---|
| Away | `away mode`, `away time` (17 B) | `0x13bf0` | `0x40274c` |
| Inching | `inching mode`, `inching time` (4 B) | `0x13e00` | `0x4027c4` |
| Saving | `saving mode`, `saving value` (4 B) | `0x13ed0` | `0x4027cc` |
| Protect | `protect mode`, `protect value`, `pretect action` *(sic)* | `0x13f4c` | `0x4027d4` |

**Over-power protection** is enforced inside the 1 Hz metering tick, and is
the safety-relevant one. Instantaneous power is compared against the protect
threshold; a **4-consecutive-sample debounce** must be satisfied before it
acts, so a startup inrush spike cannot trip it:

```
13aa6: ldr r1, [0x4027d4]     ; protect value
13aaa: cmp r4, #0             ; protect mode selects comparison direction
13b62: r4 = trip_count + 1
13b6a: cmp r4, #4 ; bls skip  ; require >4 consecutive breaches
13b72: bl 0x13518             ; force relay OFF
```

`0x13518` is `force_switch(state)`: takes the meter mutex, reads the current
relay state, writes the new one only if it differs, releases, then runs the
post-change notify hook. `0x13548` is a thin `force_off()` wrapper.

**Saving mode** is an energy budget rather than a power limit — a separate
accumulator (`0x4027a8`) is compared against `saving value` in the `"cur
timer"`, and trips the same `force_off()` path when exceeded.

**Inching mode** creates a timer whose period is `inching_time * 1000` ms —
auto-off (momentary/pulse) behaviour after a set number of seconds.

**Away mode** takes a fixed 1000 ms timer and a 17-byte time string, i.e. a
schedule window rather than a scalar.

## Relay and persistence

Relay state is authoritative in NV, not in RAM:

```
173d8: set_switch(state, persist)
         -> nv_write("switch", ...)      ; NV namespace BEKEN0
         -> 0x1738c
1738c: set_relay(state)
  17392:  ldr r0, =0x400126   ; descriptor {6,5} -> P6
  17398:  bl  gpio_output     ; drive relay
  173a0:  bl  set_led(state, 0)  ; mirror onto LED index 0
```

The `switch` key was still present in the plaintext KV region of the dump, so
the plug restored its last relay state across power loss. `state` and `net_c`
keys sit alongside it.

## Cloud / MQTT

The stock firmware ran a full MQTT client alongside Matter (`"mqtt":1` in the
identity blob). Protocol strings `MQTT` and `MQIsdp` are both present, i.e. an
MQTT 3.1.1 / 3.1 client.

### Broker address is not compiled in

There is **no broker hostname anywhere in the image**. It is read from an NV
key at connect time:

```
1808c: set_mg_domain(buf)  -> nv_write("BEKEN0", "mg_domain", buf, 68)
180a4: get_mg_domain(buf)  -> nv_read ("BEKEN0", "mg_domain", buf, 68, &len)
```

The connect routine at `0x18570` calls `get_mg_domain` and **aborts the whole
MQTT stack if the key is missing or empty** (`0x185c2: bgt` / else bail to
`0x18712`). The domain is provisioned together with `pid` and `did` through
the vendor's "marsgate" flow (`marsgate metainfo pid:%s;did:%s`), which logs
`marsgate metainfo not found` when absent.

No port literal appears either (8883/1883/443 are absent as immediates and as
literal words), so the port is carried inside the 68-byte `mg_domain` value or
defaulted by the MQTT library.

### TLS is pinned to a Uascent certificate

A PEM certificate is embedded in the image at `0xf8c8a`:

| Field | Value |
|---|---|
| Subject | `CN=*.uascent-iot.com` |
| SAN | `*.uascent-iot.com`, `uascent-iot.com` |
| Issuer | `C=US, O=DigiCert Inc, CN=RapidSSL TLS DV RSA Mixed SHA256 2020 CA-1` |
| Valid | 2021-01-20 to **2022-01-20 (expired)** |

This is a *leaf* server certificate, not a CA root — the firmware pins the
server rather than validating a chain. So the broker lived under
`uascent-iot.com`, even though the specific subdomain is only known at
provisioning time.

### Topic scheme

Topics are built with the format `%s/%s/%s/%s` (`0x1088fd`, used at `0x182a2`)
as `<prefix>/<pid>/<did>/<suffix>`:

| Direction | Topic |
|---|---|
| publish | `/v1/sys/device/<pid>/<did>/data/uplink` |
| publish | `/v1/ota/device/<pid>/<did>/inform` |
| publish | `/v1/ota/device/<pid>/<did>/progress` |
| subscribe | `/v1/sys/device/<pid>/<did>/data/downstream` |
| subscribe | `/v1/ota/device/<pid>/<did>/upgrade` |

### Application messages

JSON, keyed by `msg_type`, each request having a `_reply` counterpart:

`prod_atr`, `get_prod_atr`, `dev_reset`, `event_notify`, `device_status_notify`,
`ota_dev_info`, `ota_dev_progress`, `ota_dev_upgrade`, `unix_timestamp`.

Common fields: `sign_method`, `sign_val`, `operation` (e.g. `unbind`), `step`,
`desc`, `size`, `success`, `property`, `mac`, `spid`. The metering upload uses
`dataArray` / `powerConsumption` / `beginTime` / `endTime`.

A separate `uhome_udp` service handles local UDP discovery, and `uhome_ota`
the firmware update path.

### This unit was never cloud-provisioned

The plaintext KV region of the dump contains **no `mg_domain` key** — only
Matter data: two commissioned fabrics (one labelled `Android Local Fabric`),
`chip-factory.uniqueId`, `chip-config.country-code` = `CH`, and the `bl0937`
calibration. So this plug ran Matter-only and the MQTT client would have
bailed at `0x185c2` on every boot. Nothing was uploaded from it.

## Other subsystems

- **OTA** — HTTP download with an MD5 check and a version/size argument, per
  the usage string `ota_download 0 http://.../new.ota md5 <hash> 2.0 732063`.
- **Factory test** — a `JX` debug console over UART with commands `gpiotest`,
  `wifitest`, `heap`, `reset`, `devset`, `marsgate`, plus commissioning
  getters/setters (`dis`, `pincode`, `salt`, `verifier`, `iteration`,
  `vendor`, `product`).
- **Commissioning data** — read from a factory-data partition (`dac`, `pai`,
  `cd`, `pacPub`/`pacPri`, `spid`); logs `no factory data` when absent.
- **Provisioning** — a "marsgate" metainfo blob carrying `pid`/`did`.

## Implications for this project

1. **SEL polarity is inverted in our driver.** `src/bl0937.cpp` documents
   "LOW selects voltage, HIGH selects current"; the stock firmware does the
   opposite. Fixing this is a one-line change but silently swaps V and I if
   left alone.
2. **Calibration constants transfer directly** — same divisor convention,
   already applied in `src/bl0937.cpp`.
3. **Median-of-3 at 1 Hz with a 3 s SEL dwell** is a better-tested cadence
   than our current settle-then-read scheme, and needs no discarded window.
4. **Change-deadband reporting** (0.05 / 0.003) is worth adopting for the
   Matter attribute reports.
5. **Over-power protection with a 4-sample debounce** is a safety feature we
   do not yet have, and it is cheap to add.
