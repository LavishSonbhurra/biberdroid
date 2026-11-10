#!/bin/bash
# Build (or refresh) a minimal Android-like root with NO Termux in it:
# only termux-docker's /system (Android 9 bionic, linker64, toybox), an app
# nativeLibraryDir holding the payload's jniLibs and the assets unpacked under
# the app's filesDir, laid out as Android lays out an installed app.
# APP_ID picks the package name. HOST_JNILIBS (optional) is a folder of a host
# app's own jniLibs to add (a Tectonic build, say) for an integration test.
# Usage: approot.sh            -> builds scratch/approot
#        approot.sh run 'cmd'  -> runs cmd as an app uid (10234) with /system/bin/sh
set -e
B=$(cd "$(dirname "$0")/.." && pwd)
W=${BIBER_WORK:-$B/work}   # downloads, the rootfs and build output (gitignored)
A=$W/scratch/approot
PKG=${APP_ID:-org.example.biberhost}
NATIVE=/data/app/~~Zm9vYmFy/$PKG-AbCd/lib/arm64   # random-looking, like the real one
FILES=/data/user/0/$PKG/files
HFILES=/data/data/$PKG/files   # same folder; host-side path, since the symlink is absolute
if [ "$1" = run ]; then
  # Private binds: a shared one would take in the host's later mounts (a new
  # /dev/pts), and unmounting those here would unmount the host's own.
  for m in proc dev; do mountpoint -q $A/$m || { sudo mount --bind /$m $A/$m && sudo mount --make-private $A/$m; }; done
  shift
  exec sudo chroot --userspec=10234:10234 $A /system/bin/env -i \
    PATH=/system/bin HOME=$FILES/home TMPDIR=$FILES/home ANDROID_ROOT=/system ANDROID_DATA=/data \
    NATIVE=$NATIVE FILES=$FILES /system/bin/sh -c "$*"
fi
for m in proc dev; do mountpoint -q $A/$m && { sudo mount --make-rprivate $A/$m; sudo umount -R $A/$m; } || true; done
sudo rm -rf --one-file-system $A; sudo mkdir -p $A/proc $A/dev $A/tmp $A$NATIVE $A/data/data/$PKG/files/home
sudo cp -a $W/rootfs/data/data/com.termux/files/usr/opt/aosp $A/system   # termux-docker's /system is a symlink to this
sudo chown -R root:root $A/system; sudo chmod -R a+rX,go-w $A/system
sudo ln -s /data/data $A/data/user/0 2>/dev/null || { sudo mkdir -p $A/data/user; sudo ln -s /data/data $A/data/user/0; }
if [ -n "$HOST_JNILIBS" ]; then sudo cp "$HOST_JNILIBS"/*.so $A$NATIVE/; fi
PKGDIR=${PKGDIR:-$W/scratch/pkg}
sudo cp $PKGDIR/jniLibs/arm64-v8a/*.so $A$NATIVE/
sudo cp -a $PKGDIR/assets/biber $A$HFILES/biber
# The tree's version marker, as an app writes it on unpacking (the launcher's
# cache key includes it); made from the tree's contents when the payload has none.
if [ ! -f $PKGDIR/assets/biber/.version ]; then
  (cd $PKGDIR/assets/biber && find . -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -c1-64) |
    sudo tee $A$HFILES/biber/.version >/dev/null
fi
sudo ln -s $NATIVE/libbiber.so $A$HFILES/biber/bin/tectonic-biber
sudo chown -R root:root $A$NATIVE/..; sudo chmod -R a+rX,go-w $A$NATIVE/..
sudo chown -R 10234:10234 $A/data/data/$PKG; sudo chmod 700 $A/data/data/$PKG
echo "approot ready: native=$NATIVE files=$FILES"
