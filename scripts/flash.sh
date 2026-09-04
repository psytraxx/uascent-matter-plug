#!/bin/sh
# Flash the firmware over the Adafruit UF2 bootloader.
#
#   scripts/flash.sh           flash build/<image>/zephyr/zephyr.uf2
#   scripts/flash.sh -b        build first, then flash
#
# The board has no on-board debug probe, so `west flash` does not work: the
# UF2 file is copied to the mass-storage drive the bootloader exposes.

set -e
cd "$(dirname "$0")/.."

case "$1" in
	-b|--build) scripts/build.sh || exit 1; shift ;;
esac

# The image directory is named by sysbuild, so match it by glob rather than
# hard-coding a name that changes with the project.
UF2=$(ls build/*/zephyr/zephyr.uf2 2>/dev/null | head -1)

if [ -z "$UF2" ]; then
	echo "error: no build/*/zephyr/zephyr.uf2 -- run scripts/build.sh first" >&2
	exit 1
fi

# Find the bootloader's mass-storage volume by its label. The bootloader only
# appears after a double-tap of RESET (or after a crash), so wait for it.
find_dev() {
	lsblk -rno NAME,LABEL 2>/dev/null |
		awk '$2 == "XIAO-SENSE" { print "/dev/" $1; exit }'
}

DEV=$(find_dev)

if [ -z "$DEV" ]; then
	echo "Board is not in bootloader mode."
	echo "Double-tap RESET now; waiting up to 30s..."

	i=0
	while [ $i -lt 60 ]; do
		DEV=$(find_dev)
		[ -n "$DEV" ] && break
		sleep 0.5
		i=$((i + 1))
	done

	if [ -z "$DEV" ]; then
		echo "error: timed out waiting for the XIAO-SENSE drive" >&2
		exit 1
	fi
fi

# Mount it if the desktop has not already done so.
MNT=$(lsblk -rno MOUNTPOINT "$DEV" 2>/dev/null | head -1)

if [ -z "$MNT" ]; then
	MNT=$(udisksctl mount -b "$DEV" 2>&1 | sed -n 's/^Mounted .* at \(.*\)$/\1/p')
	if [ -z "$MNT" ]; then
		echo "error: could not mount $DEV" >&2
		exit 1
	fi
fi

echo "Flashing $UF2 -> $MNT"

# The bootloader reboots the moment the write completes, so the copy can fail
# at the very end with an I/O error even though the flash succeeded. That is
# expected, not a failure.
cp "$UF2" "$MNT/" 2>/dev/null || true
sync 2>/dev/null || true

echo "Flashed. The board reboots into the new firmware automatically."
echo "Run scripts/monitor.sh to watch the console."
