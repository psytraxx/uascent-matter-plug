#!/bin/sh
# Watch the USB CDC-ACM console.
#
#   scripts/monitor.sh         attach to the board's console
#
# Ctrl-C to exit. The port only exists while the firmware is running: if the
# board is sitting in the UF2 bootloader, there is no console to attach to.

set -e

PORT=$(ls /dev/serial/by-id/*Zephyr_Project_CDC_ACM* 2>/dev/null | head -1)

if [ -z "$PORT" ]; then
	if ls /dev/serial/by-id/*XIAO* >/dev/null 2>&1; then
		echo "The board is in the UF2 bootloader, which has no console." >&2
		echo "Press RESET once to run the firmware, then retry." >&2
	else
		echo "No board console found under /dev/serial/by-id/." >&2
		echo "Check that the board is plugged in and the firmware is running." >&2
	fi
	exit 1
fi

echo "Console: $PORT  (Ctrl-C to exit)"

# Raw mode, no echo: the console is one-way logging, and letting the terminal
# echo or line-buffer corrupts the output. Baud rate is irrelevant over USB CDC.
stty -F "$PORT" raw -echo 2>/dev/null || true
exec cat "$PORT"
