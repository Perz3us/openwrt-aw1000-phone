#!/bin/sh
# usage: urc_probe.sh <seconds>
T=${1:-120}
for p in /dev/ttyUSB2 /dev/ttyUSB3; do
	( timeout "$T" cat "$p" | while IFS= read -r l; do echo "$p: $l"; done ) &
done
wait
