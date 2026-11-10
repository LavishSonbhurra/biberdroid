#!/bin/bash
# Run a command inside the termux-docker rootfs (bionic aarch64) as the
# unprivileged "system" user, the way termux-docker's entrypoint does.
# Usage: tx.sh 'shell command'      (runs under Termux bash -lc)
#        TX_ROOT=1 tx.sh '...'      (runs as root, for mounts/debug)
set -e
R=${ROOTFS:-${BIBER_WORK:-$(cd "$(dirname "$0")/.." && pwd)/work}/rootfs}
for m in proc sys dev; do
  mountpoint -q "$R/$m" || sudo mount --bind /$m "$R/$m"
done
mountpoint -q "$R/dev/pts" || sudo mount --bind /dev/pts "$R/dev/pts"
P=/data/data/com.termux/files/usr
ENVS=(ANDROID_DATA=/data ANDROID_ROOT=/system HOME=/data/data/com.termux/files/home
      LANG=en_US.UTF-8 PATH=$P/bin PREFIX=$P TMPDIR=$P/tmp TZ=UTC TERM=xterm)
if [ -n "$TX_ROOT" ]; then
  exec sudo chroot "$R" $P/bin/env -i "${ENVS[@]}" $P/bin/bash -lc "$*"
fi
exec sudo chroot "$R" /system/bin/su -s $P/bin/env system -- -i "${ENVS[@]}" $P/bin/bash -lc "$*"
