#!/bin/sh
# Build the firmware.
#
#   scripts/build.sh           incremental build
#   scripts/build.sh -p        pristine build (wipes build/)
#
# Any other arguments are passed through to west.

set -e
cd "$(dirname "$0")/.."
. scripts/env.sh

PRISTINE=
case "$1" in
	-p|--pristine) PRISTINE=--pristine=always; shift ;;
esac

# Matter builds with LTO are memory-hungry and get OOM-killed at default
# parallelism on this machine, so the job pools are capped.
exec west build -b "$BOARD" $PRISTINE "$@" \
	-- -DCMAKE_JOB_POOLS="compile=4;link=1"
