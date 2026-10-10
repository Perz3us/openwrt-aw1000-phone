#!/bin/sh
# usage: pcm_sample.sh <samples>  -> histogram of gpio33..36 levels
N=${1:-400}
i=0
while [ $i -lt "$N" ]; do
	grep -E "gpio3[3-6] " /sys/kernel/debug/gpio | awk '{printf "%s ", $4}'
	echo
	i=$((i + 1))
done | sort | uniq -c
