#!/bin/bash
# Build libbiber.so (src/biber-launcher.c) with Termux's clang inside the
# chroot, against Termux perl's headers and libperl.so.
set -e
B=$(cd "$(dirname "$0")/.." && pwd)
W=${BIBER_WORK:-$B/work}   # downloads, the rootfs and build output (gitignored)
H=$W/rootfs/data/data/com.termux/files/home
sudo mkdir -p $H/launcher && sudo cp $B/src/biber-launcher.c $H/launcher/ && sudo chown -R 1000:1000 $H/launcher
$B/scripts/tx.sh 'cd ~/launcher && CORE=$(perl -MConfig -e "print \"\$Config{archlibexp}/CORE\"") &&
  clang -O2 -fPIE -pie -o libbiber.so biber-launcher.c $(perl -MExtUtils::Embed -e ccopts) -L$CORE -lperl -lm -ldl &&
  llvm-strip libbiber.so && ls -la libbiber.so' 2>&1 | grep -v getprop
