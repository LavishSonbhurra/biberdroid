#!/bin/bash
# biber 2.21 for Android (bionic, arm64-v8a), as a relocatable payload an app
# can ship: ELF files for jniLibs (run from nativeLibraryDir, the only place an
# app targeting SDK 29+ may execve from) and a pure-Perl tree for its assets.
#
# Steps, in order (docs/BUILDING.md has the details):
#  fetch     every download, pinned by SHA-256 (pins.txt, scripts/cpan-pins.sha256),
#            from its own URL or, failing that, from this repository's mirror release
#  rootfs    termux-docker's root filesystem, entered with sudo chroot. The Linux
#            kernel runs Android ELF files natively given Android's /system, so the
#            build runs Termux's own aarch64 perl and clang, with no emulator.
#  packages  Termux build tools from apt (not shipped, not pinned), then the pinned
#            perl, libxml2, zlib and libiconv .debs with dpkg
#  cpan      the 112 pinned CPAN distributions, in dependency order, with cpanm
#            --notest and no network access (an unpinned dependency fails the build)
#  biber     Build.PL, ./Build test (biber's 1264 tests, on bionic), install
#  launcher  src/biber-launcher.c -> libbiber.so (embeds libperl.so)
#  package   scripts/package.py  -> out/jniLibs, out/assets, out/manifest.tsv
#            scripts/licenses.py -> out/COMPONENTS.tsv, out/FILES.tsv
#            scripts/notices.py  -> out/NOTICES.txt
#  verify    a minimal Android-like root with no Termux in it (scripts/approot.sh):
#            fixture .bbl files byte-identical to the official biber 2.21, the
#            launcher's Tectonic mode, a --validate-datamodel run, and biber's own
#            test suite against the packaged tree
#  dist      release files in dist/: the payload, the jniLibs, the Perl tree as a
#            reproducible tar.xz, notices, SHA256SUMS
#
# Not run by default:
#  sources   the corresponding source of everything shipped, as one tar for the
#            release (scripts/sources.sh, source-pins.txt)
#  umount    unmounts what the chroot steps bind-mounted (run it before deleting work/)
#
# Host: aarch64 Linux with sudo, curl, python3, patchelf, tar, gzip and git
# (GitHub's ubuntu-24.04-arm runners qualify). About 2 GB of disk; under 10 minutes on
# a 2-core VM once the downloads are in place.
#
# No source of biber, Perl or any CPAN module is changed. The shipped ELF files
# are Termux's builds, changed only with patchelf (RUNPATH removed, versioned
# names made APK-safe, libandroid-utimes.so dropped from NEEDED); see
# scripts/package.py.
#
# Everything downloaded or built goes to BIBER_WORK (default: work/ here).
# Usage: ./build.sh [step...]     default: fetch rootfs packages cpan biber launcher package verify dist
set -euo pipefail
B=$(cd "$(dirname "$0")" && pwd)
W=${BIBER_WORK:-$B/work}
export BIBER_WORK=$W
S=$B/scripts
DL=$W/dl/pinned
H=$W/rootfs/data/data/com.termux/files/home
# Release name used in dist/ file names, e.g. 2.21-1 for the tag v2.21-1.
VERSION=${BIBERDROID_VERSION:-2.21-dev}
# Copies of every pinned download, kept as assets of one release of this
# repository, for when an upstream URL stops working (Termux removes old .debs).
MIRROR=${BIBER_MIRROR:-https://github.com/LavishSonbhurra/biberdroid/releases/download/pinned-downloads}
steps=${*:-fetch rootfs packages cpan biber launcher package verify dist}

check() { echo "$1  $2" | sha256sum -c --quiet - || { echo "checksum mismatch: $2" >&2; rm -f "$2"; return 1; }; }

# get SHA256 URL FILE: the upstream URL first, then the mirror.
get() {
  local sha=$1 url=$2 f=$3 name
  name=$(basename "$f")
  if [ -f "$f" ] && check "$sha" "$f"; then return 0; fi
  if download "$url" "$f" && check "$sha" "$f"; then return 0; fi
  echo "upstream failed for $name, trying the mirror" >&2
  curl -fsSL --retry 3 -o "$f" "$MIRROR/$name" && check "$sha" "$f"
}

download() {
  local url=$1 f=$2 repo digest token
  case $url in
    docker://*)
      repo=${url#docker://}; repo=${repo%@*}; digest=${url#*@}
      token=$(curl -fsS "https://auth.docker.io/token?service=registry.docker.io&scope=repository:$repo:pull" |
              python3 -I -c 'import sys,json;print(json.load(sys.stdin)["token"])') || return 1
      curl -fsSL --retry 3 -H "Authorization: Bearer $token" -o "$f" "https://registry-1.docker.io/v2/$repo/blobs/$digest" ;;
    *) curl -fsSL --retry 3 -o "$f" "$url" ;;
  esac
}

fetch() {
  mkdir -p $DL/cpan
  grep -v '^#' $B/pins.txt | while read -r sha url name; do
    [ -n "$sha" ] || continue
    get "$sha" "$url" "$DL/$name"
  done
  grep -v '^#' $S/cpan-pins.sha256 | while read -r sha url; do
    [ -n "$sha" ] || continue
    get "$sha" "$url" "$DL/cpan/$(basename "$url")"
  done
  echo "all downloads present and checked"
}

rootfs() {
  [ -d $W/rootfs/data ] && { echo "rootfs exists"; return; }
  mkdir -p $W/rootfs
  sudo tar -xzf $DL/termux-docker-layer.tar.gz -C $W/rootfs
}

packages() {
  sudo mkdir -p $H/pinned && sudo cp $DL/*.deb $H/pinned/ && sudo chown -R 1000:1000 $H/pinned
  $S/tx.sh 'apt-get update -qq && apt-get install -y -qq clang make pkg-config binutils curl libandroid-utimes libxslt &&
            dpkg -i ~/pinned/*.deb && dpkg-query -W perl libxml2 zlib libiconv' 2>&1 | grep -v getprop
}

cpan() {
  sudo mkdir -p $H/cpan && sudo cp $DL/cpan/* $H/cpan/
  grep -v '^#' $S/cpan-pins.sha256 | awk 'NF {print $2}' | xargs -n1 basename | sudo tee $H/cpan/ORDER >/dev/null
  tar -xzOf $DL/App-cpanminus-1.7049.tar.gz App-cpanminus-1.7049/bin/cpanm | sudo tee $H/cpanm >/dev/null
  sudo chown -R 1000:1000 $H/cpan $H/cpanm
  $S/tx.sh 'set -e; export PERL_MM_USE_DEFAULT=1
    while read -r t; do
      perl ~/cpanm --notest --quiet --skip-installed -L ~/biberlib --mirror file:///nonexistent --mirror-only ~/cpan/$t
    done < ~/cpan/ORDER' 2>&1 | grep -v getprop
}

biber() {
  sudo cp $DL/biber-2.21.tar.gz $H/ && sudo chown 1000:1000 $H/biber-2.21.tar.gz
  $S/tx.sh 'set -e; cd ~ && rm -rf biber-2.21 && tar xzf biber-2.21.tar.gz && cd biber-2.21 &&
    export PERL5LIB=$HOME/biberlib/lib/perl5 && perl Build.PL --install_base $HOME/biberlib &&
    ./Build && ./Build test && ./Build install' 2>&1 | grep -v getprop | tail -5
}

launcher() { $S/build-launcher.sh; }

package() {
  sudo rm -rf $W/out
  sudo python3 -I $S/package.py $W/rootfs $W/out
  sudo python3 -I $S/licenses.py $W/rootfs $W/out $DL/cpan
  sudo chown -R "$(id -u):$(id -g)" $W/out
  BIBERDROID_VERSION=$VERSION python3 -I $S/notices.py $W/out $DL/cpan
  python3 -I $S/check-elf.py $W/out/jniLibs/arm64-v8a
  # The Perl tree as an app unpacks it, and its SHA-256 (the tree's version marker).
  python3 -I $S/archive.py $W/out/assets/biber $W/out/perl-tree.tar.xz
  sha256sum $W/out/perl-tree.tar.xz | cut -d' ' -f1 > $W/out/perl-tree.sha256
}

verify() {
  PKGDIR=$W/out $S/approot.sh
  $S/compare-approot.sh
  $S/tool-approot.sh
  $S/datamodel-approot.sh
  TAIL=3 $S/suite-approot.sh
}

dist() {
  local D=$W/dist P=biberdroid-$VERSION
  rm -rf $D; mkdir -p $D
  (cd $W/out && tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -czf $D/$P-android-arm64.tar.gz \
     jniLibs assets manifest.tsv COMPONENTS.tsv FILES.tsv NOTICES.txt)
  (cd $W/out && tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -czf $D/$P-jniLibs-arm64-v8a.tar.gz jniLibs)
  cp $W/out/perl-tree.tar.xz $D/$P-perl-tree.tar.xz
  cp $W/out/NOTICES.txt $D/NOTICES.txt
  cp $W/out/COMPONENTS.tsv $D/COMPONENTS.tsv
  [ -f $W/sources/sources.tar ] && cp $W/sources/sources.tar $D/$P-sources.tar
  (cd $D && sha256sum -- * > SHA256SUMS && cat SHA256SUMS)
}

sources() { $S/sources.sh; }

umount() {
  local m
  for m in $(mount | awk -v w="$W/" 'index($3, w) == 1 {print $3}' | sort -r); do sudo umount -l "$m"; done
  echo "nothing of $W is mounted"
}

for s in $steps; do echo "== $s"; $s; done
