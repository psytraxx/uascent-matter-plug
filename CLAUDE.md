# blink

XIAO nRF52840 **Sense** firmware on the nRF Connect SDK (Zephyr).
This is a west/CMake project; do not add `platformio.ini` or Arduino sources.

The app is a **Matter smart plug**: the XIAO replaces the BK7231N module in an
existing BL0937 mains plug, keeping the plug's relay, meter chip, button, and
LED. See `docs/smart-plug-plan.md` for the design and `README.md` for usage.

**Mains safety:** the BL0937 is not isolated from mains. Never connect USB
while the plug is on mains. Develop USB-powered with mains disconnected.

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
west build -b xiao_ble
```

Matter builds are memory-hungry with LTO and get OOM-killed (exit 137) on this
machine at default parallelism. Pass
`-- -DCMAKE_JOB_POOLS="compile=4;link=1"` for those.

## Hardware

Board target is **`xiao_ble`** (plain, no IMU/microphone) — confirmed by
physical inspection of the board. The plain and Sense variants share the
same D0–D10 connector pinout (`seeed_xiao_connector.dtsi` is common to
both); Sense only adds IMU/microphone devicetree nodes and enables I2C0 for
them, so nothing pin-related depends on getting this choice right beyond
those two unused peripherals.

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

Two console settings matter for Matter, since there is only one CDC-ACM port:

* `CONFIG_CHIP_LIB_SHELL=n`. The light_switch sample turns the Matter shell
  on, which assumes a UART separate from the log backend. Here the shell
  prompt races the log output and both come out garbled.
* `CONFIG_LOG_MODE_DEFERRED=y`. The Matter Kconfig defaults pick
  `LOG_MODE_MINIMAL`, which *drops* messages rather than buffering them, so
  the onboarding-code block is truncated mid-line during the boot burst.

The 1200-baud touch does not put this board into the bootloader (that is an
`usb_device` feature and this board runs `usb_device_next`), so entering UF2
mode means physically double-tapping RESET.

## Matter

Started from NCS `samples/matter/light_switch`; the data model is now an
On/Off Plug-in Unit (0x010A) with ElectricalPowerMeasurement (0x0090) and
ElectricalEnergyMeasurement (0x0091) on endpoint 1.

There is **no ZAP GUI in this environment**. `src/default_zap/smart_plug.zap`
and everything under `src/default_zap/zap-generated/` are hand-maintained
against the ZCL XML in
`modules/lib/matter/src/app/zap-templates/zcl/data-model/chip/`. Note the
build's `codegen.py` step parses the `.matter` IDL file, not the `.zap`, so
the `.matter` cluster definitions must stay complete and consistent.

Three board constraints follow from the UF2 flash map having no
`slot0_partition`, and each has to be disabled in a particular way:

* `SB_CONFIG_BOOTLOADER_NONE=y` -- no MCUboot.
* `CONFIG_CHIP_FACTORY_DATA_NONE=y` and `CONFIG_CHIP_BOOTLOADER_NONE=y` --
  both are `choice` branches, so they are selected rather than assigned `=n`
  (the MCUboot branch hard-`select`s `IMG_MANAGER`, which `=n` cannot undo).
* `SB_CONFIG_MATTER_OTA=n` in `sysbuild.conf` -- **not** `prj.conf`. Sysbuild
  writes `CONFIG_CHIP_OTA_REQUESTOR` into the app config *after* `prj.conf`
  and the app `Kconfig` are merged, so setting it there has no effect.

Onboarding credentials are compiled in from `prj.conf`
(`CHIP_DEVICE_SPAKE2_PASSCODE` / `CHIP_DEVICE_DISCRIMINATOR`) rather than
generated, so the pairing code is fixed; this only works while factory data is
disabled. They are currently the Matter test defaults.

The Matter common board layer requires a devicetree `/buttons` node, which
this board lacks; `boards/xiao_ble.overlay` adds one, plus the
plug's relay, LED, and BL0937 pins.

Those extra nodes reuse the `gpio-leds`/`gpio-keys` bindings even where the
signal is neither an LED nor a key. That is required, not cosmetic: a node
with no `compatible` gets no binding, so its `gpios` property is never
expanded into the form `GPIO_DT_SPEC_GET()` needs, and the build fails.

The board layer's function button is `DK_BTN1`, i.e. the first `/buttons`
child. With only one button declared, `DK_BTN2_MSK` can never fire.

Flash is ~83% full, so `CONFIG_LTO=y` is required to fit.
