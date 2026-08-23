#!/bin/sh
# Shared nRF Connect SDK environment. Nothing from the SDK is on PATH by
# default, so every script sources this first.
#
# Sourced, not executed: it only exports variables.

NCS_ROOT=${NCS_ROOT:-$HOME/ncs}
NCS_VERSION=${NCS_VERSION:-v3.4.0}
NCS_TOOLCHAIN=${NCS_TOOLCHAIN:-fbf7391cab}

T="$NCS_ROOT/toolchains/$NCS_TOOLCHAIN"

if [ ! -d "$T" ]; then
	echo "error: toolchain not found at $T" >&2
	echo "set NCS_ROOT / NCS_TOOLCHAIN if the SDK lives elsewhere" >&2
	exit 1
fi

# ccache is picked up from PATH by the Zephyr build; keeping it ahead of the
# toolchain entries means compiles go through the shared cache in
# ~/.cache/ccache, the same one other projects on this machine use.
NCS_PATH="$T/bin:$T/usr/bin:$T/usr/local/bin:$T/opt/bin"
NCS_PATH="$NCS_PATH:$T/opt/nanopb/generator-bin:$T/nrfutil/bin"
NCS_PATH="$NCS_PATH:$T/opt/zephyr-sdk/gnu/arm-zephyr-eabi/bin"

# The toolchain goes in front, but the system PATH has to stay on the end:
# the Matter build shells out to plain coreutils (touch, sed, ...) that the
# toolchain does not ship.
PATH="$NCS_PATH:$PATH"
export PATH

LD_LIBRARY_PATH="$T/lib:$T/lib/x86_64-linux-gnu:$T/usr/local/lib:$LD_LIBRARY_PATH"
export LD_LIBRARY_PATH

export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR="$T/opt/zephyr-sdk"
export ZEPHYR_BASE="$NCS_ROOT/$NCS_VERSION/zephyr"

export BOARD=${BOARD:-xiao_ble/nrf52840/sense}
