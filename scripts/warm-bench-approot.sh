#!/bin/bash
# Times a biber run on its own against one a warm process did, and the warm
# process's start, in the minimal app root (approot.sh). Medians of RUNS runs
# (31), taken in turns. Usage: warm-bench-approot.sh [FIXTURE]
set -e
B=$(cd "$(dirname "$0")/.." && pwd)
W=${BIBER_WORK:-$B/work}   # downloads, the rootfs and build output (gitignored)
A=$W/scratch/approot
PKG=${APP_ID:-org.example.biberhost}
for m in proc dev; do mountpoint -q $A/$m || { sudo mount --bind /$m $A/$m && sudo mount --make-private $A/$m; }; done
sudo python3 -I $B/tests/warm/bench.py $A "/data/app/~~Zm9vYmFy/$PKG-AbCd/lib/arm64" /data/user/0/$PKG/files \
  $B/tests/fixtures/${1:-t1-numeric} ${RUNS:-31}
