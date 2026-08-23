#!/bin/sh
# Watch the USB CDC-ACM console.
#
#   scripts/monitor.sh          attach, and reattach across reboots
#   scripts/monitor.sh --once   attach once, exit when the port goes away
#
# Ctrl-C to exit.
#
# The console is emulated by the nRF52840 itself, so the port disappears
# whenever the firmware resets and comes back when it reboots. By default this
# script waits for it to return rather than exiting, which keeps one terminal
# usable across reflashes and crash loops.
#
# Note there is no host-side reset: on this board DTR/RTS are not wired to the
# reset pin (there is no separate USB-serial chip to wire them to), so nothing
# the host sends can restart the MCU. Use the RESET button, or double-tap it
# for the UF2 bootloader.

set -e

ONCE=
case "$1" in
	--once) ONCE=1 ;;
esac

find_port() {
	ls /dev/serial/by-id/*Zephyr_Project_CDC_ACM* 2>/dev/null | head -1
}

in_bootloader() {
	lsblk -rno LABEL 2>/dev/null | grep -q '^XIAO-SENSE$'
}

attach() {
	port=$1
	echo "--- console: $port ---"

	# Raw mode, no echo: this is one-way logging, and letting the terminal
	# echo or line-buffer corrupts it. Baud rate is irrelevant over USB CDC.
	stty -F "$port" raw -echo 2>/dev/null || true

	# Exits when the device node disappears, i.e. when the board resets.
	cat "$port" 2>/dev/null || true
}

PORT=$(find_port)

if [ -z "$PORT" ] && [ -n "$ONCE" ]; then
	if in_bootloader; then
		echo "The board is in the UF2 bootloader, which has no console." >&2
		echo "Press RESET once to run the firmware." >&2
	else
		echo "No board console found under /dev/serial/by-id/." >&2
	fi
	exit 1
fi

[ -n "$PORT" ] || echo "Waiting for the board console... (Ctrl-C to exit)"

while :; do
	PORT=$(find_port)

	if [ -n "$PORT" ]; then
		attach "$PORT"
		[ -n "$ONCE" ] && exit 0
		echo "--- console closed (board reset or unplugged); waiting ---"
	fi

	sleep 0.5
done
