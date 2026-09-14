#!/bin/bash
# Symbolize "prof: <pc> <count> <pct>" lines from a capture: tools/symbolize.sh NOTES/logs/x.log
cd "$(dirname "$0")/.."; export LC_ALL=C
tr -d '\000' < "$1" | sed 's/\x1b\[[0-9;]*m//g' | grep -aE '^prof: [0-9a-f]{8} ' | awk '{c[$2]+=$3; t+=$3} END {for (k in c) printf "%s %d %.1f\n", k, c[k], 100*c[k]/t}' | sort -k2 -rn | head -50 > build_docker/prof_pcs.txt
docker run --rm -v "$PWD":/project -w /project/build_docker espressif/idf:v5.3.4 bash -c "while read pc n pct; do printf '%5s%%  %s  ' \$pct \$pc; riscv32-esp-elf-addr2line -f -C -e diablito.elf 0x\$pc | head -1; done < /project/build_docker/prof_pcs.txt" 2>/dev/null < /dev/null | grep -vE '^(Detecting|Checking|Adding|Python|Requirement| -|Added|  /|Done|Go to|  idf|"python3"|$)'
