# blink

XIAO nRF52840 **Sense** firmware on the nRF Connect SDK (Zephyr).
This is a west/CMake project; do not add `platformio.ini` or Arduino sources.

## Toolchain

NCS **v3.4.0** at `/home/eric/ncs`, toolchain `fbf7391cab`. Nothing is on
`PATH` — set the environment up before calling `west`:

```sh
T=/home/eric/ncs/toolchains/fbf7391cab
export PATH=$T/bin:$T/usr/bin:$T/usr/local/bin:$T/opt/bin:$T/opt/nanopb/generator-bin:$T/nrfutil/bin:$T/opt/zephyr-sdk/gnu/arm-zephyr-eabi/bin:$PATH
export LD_LIBRARY_PATH=$T/lib:$T/lib/x86_64-linux-gnu:$T/usr/local/lib:$LD_LIBRARY_PATH
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR=$T/opt/zephyr-sdk
export ZEPHYR_BASE=/home/eric/ncs/v3.4.0/zephyr
west build -b xiao_ble/nrf52840/sense
```

Matter builds are memory-hungry with LTO and get OOM-killed (exit 137) on this
machine at default parallelism. Pass
`-- -DCMAKE_JOB_POOLS="compile=4;link=1"` for those.

## Hardware

Board target is **`xiao_ble/nrf52840/sense`**. The `sense` variant matters: the
plain `xiao_ble/nrf52840` target has a different pin map.

USB IDs: `2886:0045` is the **UF2 bootloader**, which also exposes the
`XIAO-SENSE` mass-storage drive. With this firmware running, the board
enumerates as `Zephyr_Project_CDC_ACM_serial_backend`, not under Seeed's VID.

LEDs are active-low on `led0` = red, `led1` = green, `led2` = blue.

The board has **no on-board debug probe**, so `west flash` does not work.
Flash by double-tapping RESET and copying `build/blink/zephyr/zephyr.uf2` to
the mass-storage drive that appears. The app partition starts at `0x27000`
(the Adafruit UF2 bootloader's entry point) and is `0xC5000` (788 KB).

Console is USB CDC-ACM, supplied by the board devicetree via
`cdc_acm_serial.dtsi`. Do **not** enable `CONFIG_USB_DEVICE_STACK`: the board
uses the newer `usb_device_next` stack and enabling both yields duplicate
`cdc_acm` symbols at link time.

## Matter

Matter over Thread does not build for this board and is not part of the
build; its configuration lives in `matter-wip/`. See the "Matter status"
section of [README.md](README.md) for the board constraints (MCUboot, factory
data) and the open `CHIP_OTA_REQUESTOR` blocker.
