# blink

Seeed **XIAO nRF52840 Sense** firmware on the nRF Connect SDK (Zephyr).

Cycles the RGB status LED through three indications:

| Indication | Meaning |
| --- | --- |
| Blue, blinking | Pairing / commissioning window open |
| Green, solid | Commissioned onto a fabric |
| Red, solid | Error |

Matter over Thread is not enabled; see [Matter status](#matter-status).

## Prerequisites

nRF Connect SDK **v3.4.0** at `/home/eric/ncs`, with toolchain `fbf7391cab`.
The SDK is not on `PATH`; the commands below assume the environment described
in [CLAUDE.md](CLAUDE.md), or use the VS Code nRF Connect extension.

## Build

```sh
west build -b xiao_ble/nrf52840/sense
```

The board target must include the `sense` variant. Plain `xiao_ble/nrf52840`
selects the non-Sense board, which has a different pin map.

Current footprint: ~60 KB flash (7.5% of the 788 KB app partition), ~18 KB RAM.

## Flash

The XIAO ships the **Adafruit UF2 bootloader** and has no on-board debug
probe, so flashing is drag-and-drop rather than `west flash`:

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
minicom -D /dev/serial/by-id/usb-Zephyr_Project_CDC_ACM_serial_backend_*-if00
```

Any serial terminal works; the baud rate is irrelevant for USB CDC.

## Matter status

Matter over Thread does **not** build for this board. The configuration for it
lives in `matter-wip/` (app task, ZAP data model, Kconfig, sysbuild config)
and is not part of the build.

Constraints that apply to this board:

* The Matter library itself (`libCHIP.a`) builds cleanly for
  `xiao_ble/nrf52840/sense`.
* MCUboot cannot be built: the UF2 flash map defines no `slot0_partition`,
  so the bootloader must be `SB_CONFIG_BOOTLOADER_NONE=y`.
* `CHIP_FACTORY_DATA` is reached through a `choice`, so it is disabled by
  selecting `CONFIG_CHIP_FACTORY_DATA_NONE=y`, not by assigning it `=n`.
* `CHIP_OTA_REQUESTOR` defaults to `y` and `imply`s `IMG_MANAGER` /
  `STREAM_FLASH`. Those need a `slot0_partition` this board does not have,
  and disabling the implied leaves in `prj.conf` trips a Kconfig
  `select`/`depends on` conflict. This is the open blocker.

Enabling Matter needs either a devicetree overlay adding MCUboot-style
partitions (which gives up UF2 drag-drop flashing and requires SWD to
recover), or a cleaner way to switch the OTA requestor off. Reference layout:
`$NCS/nrf/dts/samples/matter/nrf52840_partitions.dtsi`.
