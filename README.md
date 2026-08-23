# blink

Seeed **XIAO nRF52840 Sense** firmware on the nRF Connect SDK (Zephyr).

A **Matter over Thread light switch**, ported from the NCS
`samples/matter/light_switch` sample. It binds to a Matter lighting device and
toggles or dims it.

The on-board RGB LED shows the commissioning state:

| Indication | Meaning |
| --- | --- |
| Blue, blinking | Unprovisioned: commissioning window open (pairing mode) |
| Green, solid | Provisioned onto a Matter fabric |
| Red, solid | Not provisioned and not advertising -- check the console |

See [Matter](#matter) for the pairing code and the board-specific
configuration.

## Prerequisites

nRF Connect SDK **v3.4.0** at `/home/eric/ncs`, with toolchain `fbf7391cab`.
The SDK is not on `PATH`; the commands below assume the environment described
in [CLAUDE.md](CLAUDE.md), or use the VS Code nRF Connect extension.

## Scripts

`scripts/` wraps the three things you actually do. They set the SDK
environment themselves, so they work from a plain shell:

| Script | Purpose |
| --- | --- |
| `scripts/build.sh` | Build (`-p` for a pristine rebuild) |
| `scripts/flash.sh` | Flash over UF2 (`-b` to build first) |
| `scripts/monitor.sh` | Watch the USB console |

They assume the SDK layout in [CLAUDE.md](CLAUDE.md); override with
`NCS_ROOT`, `NCS_VERSION`, or `NCS_TOOLCHAIN` if it lives elsewhere.

Typical loop:

```sh
scripts/flash.sh -b     # build, then flash (double-tap RESET when asked)
scripts/monitor.sh      # watch it boot
```

## Build

```sh
scripts/build.sh
```

or, with the environment from [CLAUDE.md](CLAUDE.md) already set up:

```sh
west build -b xiao_ble/nrf52840/sense
```

The board target must include the `sense` variant. Plain `xiao_ble/nrf52840`
selects the non-Sense board, which has a different pin map.

Compiles go through `ccache` automatically when it is on `PATH`.

Current footprint: ~695 KB flash (86% of the 788 KB app partition), ~166 KB
RAM. The flash headroom is thin -- `CONFIG_LTO=y` is required to fit.

## Flash

```sh
scripts/flash.sh
```

The XIAO ships the **Adafruit UF2 bootloader** and has no on-board debug
probe, so flashing is drag-and-drop rather than `west flash`. The script
automates the steps below, waiting for the drive to appear:

1. Double-tap the RESET button. The board re-enumerates as USB `2886:0045` (bootloader)
   and mounts as a mass-storage drive (`XIAO-SENSE`).
2. Copy the UF2 onto it:

   ```sh
   cp build/blink/zephyr/zephyr.uf2 /run/media/$USER/XIAO-SENSE/
   ```

3. The board reboots into the new firmware automatically.

`west flash` will not work without an external SWD probe.

## Monitor

The console is USB CDC-ACM, provided by the board's devicetree
(`cdc_acm_serial.dtsi`). After the firmware boots:

```sh
scripts/monitor.sh
```

or any serial terminal; the baud rate is irrelevant for USB CDC:

```sh
minicom -D /dev/serial/by-id/usb-Zephyr_Project_CDC_ACM_serial_backend_*-if00
```

The console only exists while the firmware is running. In the UF2 bootloader
the board enumerates as `2886:0045` with no serial port.

Two console settings are load-bearing on this board, both driven by there
being a single CDC-ACM port shared by everything:

* The Matter shell (`CONFIG_CHIP_LIB_SHELL`) is **off**. The light_switch
  sample enables it, but it assumes a UART separate from the log backend; on
  one shared port its prompt races the log output and corrupts both.
* `CONFIG_LOG_MODE_DEFERRED=y`. The default `LOG_MODE_MINIMAL` drops messages
  under load rather than buffering them, which truncated the onboarding-code
  block mid-line during the boot burst.

## Matter

The device is a Matter over Thread light switch. It needs a Thread border
router and a Matter controller (for example `chip-tool`) to commission.

### Pairing code

The onboarding credentials are **compiled into the image** from `prj.conf`
rather than being generated per-device, so the pairing code is known ahead of
time and does not have to be read off the serial console:

```
CONFIG_CHIP_DEVICE_DISCRIMINATOR=0xF00
CONFIG_CHIP_DEVICE_SPAKE2_PASSCODE=20202021
```

That is passcode `20202021`, discriminator `0xF00` -- manual pairing code
**3497-011-2332**. These are the Matter *test* defaults: they are fine for
bench work, but change both before using this anywhere real. The firmware also
prints the authoritative pairing code and QR payload to the console at boot,
which is worth checking against after changing either value.

This works because factory data is disabled on this board (see below); with
`CHIP_FACTORY_DATA` enabled, the credentials would come from a flash partition
instead and these Kconfig values would be ignored.

### Buttons

The Matter common board layer requires a devicetree `/buttons` node, and the
XIAO has no on-board user button. `boards/xiao_ble_nrf52840_sense.overlay`
declares two on pads **D0** (P0.02, function button) and **D1** (P0.03, switch
button), both active-low with an internal pull-up: wire a momentary switch
from the pad to ground. The firmware builds and commissions without anything
wired up; only the button-driven actions (factory reset, toggle/dim) need it.

### Board-specific configuration

The XIAO ships the Adafruit UF2 bootloader, whose flash map has no MCUboot
`slot0_partition`. Everything that assumes one has to be switched off, and
each of these is disabled in a specific way for a specific reason:

* **MCUboot** cannot be built at all: `SB_CONFIG_BOOTLOADER_NONE=y`.
* **Factory data** is reached through a `choice`, so it is disabled by
  selecting the branch (`CONFIG_CHIP_FACTORY_DATA_NONE=y`), not by assigning
  `=n`. Same for the Matter bootloader choice
  (`CONFIG_CHIP_BOOTLOADER_NONE=y`), whose MCUboot branch hard-`select`s
  `IMG_MANAGER` -- and a `select` cannot be overridden with `=n`.
* **The OTA requestor** is disabled from `sysbuild.conf`
  (`SB_CONFIG_MATTER_OTA=n`), *not* from `prj.conf`. Sysbuild force-writes
  `CONFIG_CHIP_OTA_REQUESTOR` into the application config after `prj.conf` and
  the application `Kconfig` are merged, so an assignment in either is silently
  overwritten. This was the long-standing blocker on building Matter for this
  board; setting it here is what resolved it.

The consequence is no OTA firmware update: the board is flashed over UF2, and
`CONFIG_CHIP_OTA_IMAGE_BUILD` is off, so no OTA image is produced.
