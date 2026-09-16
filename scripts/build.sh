#!/bin/sh
# Build the firmware.
#
#   scripts/build.sh              incremental build, default hardware variant
#   scripts/build.sh -p           pristine build (wipes the build dir)
#   VARIANT=v2 scripts/build.sh   build a different hardware variant (see
#                                 boards/xiao_ble_v2.{overlay,conf})
#
# Any other arguments are passed through to west.

set -e
cd "$(dirname "$0")/.."
. scripts/env.sh

PRISTINE=
case "$1" in
	-p|--pristine) PRISTINE=--pristine=always; shift ;;
esac

FILE_SUFFIX_ARG=
[ -n "$VARIANT" ] && FILE_SUFFIX_ARG="-DFILE_SUFFIX=$VARIANT"

# Matter builds with LTO are memory-hungry and get OOM-killed at default
# parallelism on this machine, so the job pools are capped.
exec west build -b "$BOARD" -d "$BUILD_DIR" $PRISTINE "$@" \
	-- -DCMAKE_JOB_POOLS="compile=4;link=1" $FILE_SUFFIX_ARG
