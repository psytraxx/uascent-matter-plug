# uascent-matter-plug

Firmware turning a **Seeed XIAO nRF52840 Sense** into a **Matter smart plug**.

It replaces the wireless module in an existing mains plug. The plug's own
board keeps doing the electrical work — relay, power meter, button, LED — and
this board becomes its brain, speaking Matter over Thread instead of the
vendor's cloud.

> **Status: not finished.** The device commissions and the data model is in
> place, but the relay is not yet wired to Matter commands and the power meter
> is not implemented. See [Current state](#current-state).

## ⚠️ Mains safety

The plug's power-measuring chip (BL0937) is **not isolated from mains**. Its
ground sits at mains potential.

**Never plug the board into USB while the plug is connected to mains.** That
would put your computer's USB ground at mains potential.

Develop with the board USB-powered and **mains disconnected**. Everything
except real power readings works that way. See
[docs/smart-plug-plan.md](docs/smart-plug-plan.md).

## Hardware

| Signal | XIAO pad | Purpose |
| --- | --- | --- |
| Button | D0 | Short press = toggle relay, long press (3 s) = factory reset |
| Relay | D6 | Switches the load |
| Plug LED 1 | P0.17 | The plug's own LED, tracks relay state |
| Plug LED 2 | D1 | The plug's second LED, tracks network/commissioning state |
| BL0937 CF | D2 | Power pulses from the meter chip |
| BL0937 CF1 | D3 | Voltage/current pulses |
| BL0937 SEL | D4 | Selects which of the two CF1 reports |

**These pin choices are provisional.** The plug's board has not been traced
yet, so they are guesses that are electrically sensible. They live in
`boards/xiao_ble.overlay` — correcting them is a one-file
change, no code edits. Full rationale in
[docs/smart-plug-plan.md](docs/smart-plug-plan.md).

### LEDs

The plug's board has two LEDs, each tracking one thing, plus the XIAO's own
on-board RGB as a debug mirror. Commissioning state never competes with
relay state because they're on separate LEDs.

| Situation | Plug LED 1 (relay) | Plug LED 2 (network) | Board's RGB LED |
| --- | --- | --- | --- |
| Pairing mode | mirrors relay | blinking | blue, blinking |
| Paired, load on | on | off | green |
| Paired, load off | off | off | off |
| Fault | fast blink | fast blink | red, fast blink |

## Prerequisites

nRF Connect SDK **v3.4.0** at `/home/eric/ncs`, toolchain `fbf7391cab`. Not on
`PATH` — the scripts below set it up themselves. Override with `NCS_ROOT`,
`NCS_VERSION`, `NCS_TOOLCHAIN` if your SDK is elsewhere.

### Installing the toolchain

There is **no distro package** for any of this — not in `pacman`/`paru`, not
in AUR. Nordic ships the SDK and its toolchain as bundles you install with
their own tool, `nrfutil`. The whole thing lands in one directory (`~/ncs` by
default) and touches nothing else on the system.

**1. Get `nrfutil`.** A single static binary:

```sh
mkdir -p ~/.local/bin
curl -L https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/x86_64-unknown-linux-gnu/nrfutil \
  -o ~/.local/bin/nrfutil
chmod +x ~/.local/bin/nrfutil
```

Make sure `~/.local/bin` is on your `PATH`.

**2. Add the SDK manager command.** `nrfutil` is a shell; its features are
installed as subcommands:

```sh
nrfutil install sdk-manager
```

**3. Download and install the SDK + toolchain.** One command fetches both
(about 4.3 GB of downloads, ~10 GB installed):

```sh
nrfutil sdk-manager install v3.4.0
```

This creates:

```
~/ncs/
├── downloads/                the tarballs it fetched
│   ├── ncs-toolchain-x86_64-linux-<bundle-id>.tar.gz   (~1.2 GB)
│   └── sdk-nrf-bundle-v3.4.0.tar.gz                    (~3.1 GB)
├── toolchains/<bundle-id>/   Zephyr SDK, arm-zephyr-eabi GCC, CMake, Ninja,
│                             Python + west, nrfutil
└── v3.4.0/                   SDK sources: zephyr, nrf, modules
                              (incl. modules/lib/matter)
```

Use `--install-dir <path>` to put it elsewhere.

**4. Note the toolchain bundle ID.** `sdk-manager` names the toolchain
directory after a bundle hash — this project was built against `fbf7391cab`,
but a fresh install may differ. Check it:

```sh
nrfutil sdk-manager list
ls ~/ncs/toolchains/
```

If yours differs, tell the build scripts:

```sh
export NCS_TOOLCHAIN=<your-bundle-id>
```

`NCS_ROOT` (default `~/ncs`) and `NCS_VERSION` (default `v3.4.0`) work the
same way. See [scripts/env.sh](scripts/env.sh).

**Nothing goes on your `PATH`.** `west`, `cmake`, `ninja` and the ARM compiler
live inside `~/ncs/toolchains/<bundle-id>/` and are reached only by the
environment the scripts export — so `which west` in a fresh shell correctly
reports nothing. That is expected; run builds through `scripts/build.sh`.

Nordic also offers a GUI installer, [nRF Connect for
Desktop](https://www.nordicsemi.com/Products/Development-tools/nrf-connect-for-desktop),
whose Toolchain Manager does the same thing.

**J-Link is not needed.** The XIAO has no on-board debug probe; flashing goes
over UF2 drag-and-drop (see [Flashing](#flashing)).

## Usage

```sh
scripts/build.sh -b     # build (-p for a clean rebuild)
scripts/flash.sh -b     # build, then flash
scripts/monitor.sh      # watch the console
```

Typical loop:

```sh
scripts/flash.sh -b     # double-tap RESET when asked
scripts/monitor.sh
```

### Flashing

No debug probe on this board, so `west flash` does not work. Flashing is
drag-and-drop:

1. Double-tap RESET. The board appears as a USB drive named `XIAO-SENSE`.
2. Copy `build/uascent-matter-plug/zephyr/zephyr.uf2` onto it.
3. It reboots into the new firmware.

`scripts/flash.sh` does this for you and waits for the drive.

### Console

USB serial. `scripts/monitor.sh` reconnects by itself across resets and
reflashes, so you can leave it running.

The console only exists while the firmware runs — in bootloader mode there is
no serial port. Nothing the host sends can reset the board (the serial port is
emulated by the chip itself, so there is no DTR-to-reset wire). Use the RESET
button.

## Continuous integration

`.github/workflows/build.yml` builds the firmware on every push to `main`,
every pull request, and on demand via *Run workflow*. It runs in Nordic's
official toolchain image, which happens to ship the exact toolchain hash this
project pins (`fbf7391cab`) under `/opt/ncs/toolchains/` — the same layout
`scripts/env.sh` expects — so CI runs the same scripts you do, with only
`NCS_ROOT=/opt/ncs` set differently.

Two caches keep it quick, since a Matter build from cold is slow:

* **The SDK** (~4.7 GB) is not in the image, so it is fetched with `west` and
  cached against `NCS_VERSION`. It is refetched only when the version changes.
* **ccache** carries compiled objects between runs. CI builds pristine every
  time (`scripts/build.sh -p`), so the hit rate — not an incremental build
  directory — is what makes a run fast. The cache is keyed per commit and
  falls back to the branch, then `main`.

Each run publishes `zephyr.uf2`, `zephyr.hex`, and the resolved `.config` as
artifacts, and prints flash/RAM usage to the run summary. That last number is
worth watching: flash sits around 83% full, and `CONFIG_LTO=y` is required to
fit at all, so a change that pushes it over shows up as a failed link.

## Pairing

Needs a Thread border router and a Matter controller (e.g. `chip-tool`).

The pairing code is **compiled in**, not per-device:

```
Manual pairing code: 3497-011-2332
Passcode:            20202021
Discriminator:       0xF00
```

These are Matter's **test defaults** — fine for the bench, change them
(`prj.conf`) before anything real. The firmware also prints the code and a QR
payload to the console at boot.

## Current state

| Works | Not yet |
| --- | --- |
| Commissions onto a Matter fabric | Matter On/Off actually switching the relay |
| Data model: On/Off + power + energy | Button toggling the relay |
| Relay driver, LED indication | Reading the BL0937 power meter |
| Long-press factory reset | |

Sending On/Off from a controller **reports success but does nothing** — the
cluster is not yet connected to the relay. That is the next step.

Footprint: ~654 KB flash (83% of the 788 KB app partition), ~178 KB RAM.
`CONFIG_LTO=y` is required to fit.

## Why this board is configured oddly

Notes for anyone changing the build. Each of these is load-bearing.

**Flashing/boot.** The board's UF2 bootloader leaves no room for the usual
Matter update machinery, so it is all disabled — and each has to be disabled a
specific way:

* `SB_CONFIG_BOOTLOADER_NONE=y` — no MCUboot.
* `CONFIG_CHIP_FACTORY_DATA_NONE=y`, `CONFIG_CHIP_BOOTLOADER_NONE=y` — these
  are Kconfig *choices*, so you pick the "none" branch rather than writing
  `=n`, which would not work.
* `SB_CONFIG_MATTER_OTA=n` must be in **`sysbuild.conf`**, not `prj.conf` —
  sysbuild overwrites the `prj.conf` value afterwards. This was the
  long-standing blocker on building Matter for this board.

Consequence: no over-the-air updates. Flash over UF2.

**Console.** One USB serial port shared by everything, so:

* Matter shell off (`CONFIG_CHIP_LIB_SHELL=n`) — it assumes its own port and
  garbles the log otherwise.
* `CONFIG_LOG_MODE_DEFERRED=y` — the default drops messages under load, which
  cut off the pairing code during boot.
* Do **not** enable `CONFIG_USB_DEVICE_STACK`. This board uses the newer USB
  stack; enabling both breaks the link.

**Board target** is `xiao_ble` (plain, no IMU/microphone) — confirmed by
physical inspection. Both variants share the same D0–D10 connector pinout,
so this only matters for the unused IMU/microphone peripherals.

## Docs

* [docs/smart-plug-plan.md](docs/smart-plug-plan.md) — full design, bring-up
  order, and what is left to do.
* [docs/pinout.md](docs/pinout.md) — the XIAO module's own pin reference.
* [CLAUDE.md](CLAUDE.md) — toolchain setup details.
