#!/bin/bash
# Collects the complete source of everything the payload ships into one tar,
# work/sources/sources.tar, which each release carries as
# biber-android-<version>-sources.tar:
#
#   pinned/        biber, every CPAN distribution and cpanm, as downloaded by fetch
#   upstream/      Perl (with perl-cross, which Termux builds it with), libxml2,
#                  zlib and libiconv source tarballs (source-pins.txt)
#   termux/        Termux's recipes and patches for those four packages, and its
#                  build scripts, at the commit named in source-pins.txt
#   biber-android/ this repository at the commit being built
#
# Run after fetch. Needs curl, git and tar. Uses BIBER_MIRROR as build.sh does.
set -euo pipefail
B=$(cd "$(dirname "$0")/.." && pwd)
W=${BIBER_WORK:-$B/work}
MIRROR=${BIBER_MIRROR:-https://github.com/LavishSonbhurra/biber-android/releases/download/pinned-downloads}
DL=$W/dl/sources
T=$W/sources/tree
mkdir -p $DL
rm -rf $W/sources; mkdir -p $T/upstream $T/termux $T/pinned

check() { echo "$1  $2" | sha256sum -c --quiet - 2>/dev/null; }

grep -v '^#' $B/source-pins.txt | while read -r sha url name rest; do
  [ -n "$sha" ] || continue
  if [ "$sha" = termux-packages ]; then
    commit=$url
    g=$W/sources/termux-packages
    git init -q $g
    git -C $g remote add origin https://github.com/termux/termux-packages
    git -C $g fetch -q --depth 1 --filter=blob:none origin $commit
    # Only the named folders (and the files at the top) are downloaded.
    git -C $g sparse-checkout set $name $rest
    git -C $g checkout -q $commit
    rm -rf $g/.git
    mv $g $T/termux/termux-packages-$commit
    continue
  fi
  f=$DL/$name
  if ! { [ -f $f ] && check $sha $f; }; then
    curl -fsSL --retry 3 -o $f "$url" && check $sha $f ||
      { echo "upstream failed for $name, trying the mirror" >&2; curl -fsSL --retry 3 -o $f "$MIRROR/$name" && check $sha $f; } ||
      { echo "could not get $name" >&2; exit 1; }
  fi
  cp $f $T/upstream/
done

cp -a $W/dl/pinned/. $T/pinned/
rm -f $T/pinned/termux-docker-layer.tar.gz $T/pinned/*.deb   # build tools and binaries, not source
if git -C $B rev-parse --verify -q HEAD >/dev/null; then
  git -C $B archive --format=tar --prefix=biber-android/ HEAD | tar -x -C $T
fi
cat > $T/README.txt <<'EOF'
The complete source of a biber-android release.

pinned/         biber 2.21 and the CPAN distributions it uses, exactly as the
                build downloaded them (unchanged; cpanm is a build tool only)
upstream/       the upstream source of Perl 5.44.0 (built with perl-cross 1.6.5),
                libxml2 2.15.4, zlib 1.4 and libiconv 1.19.1
termux-packages-<commit>/
                Termux's recipes and patches for those packages, and the scripts
                that build them (https://github.com/termux/termux-packages)
biber-android/  the build scripts, the launcher and the Android library

The shipped libperl.so, libxml216.so, libz1.so and libiconv.so are Termux's
builds of these sources, changed afterwards only with patchelf, as
biber-android/scripts/package.py describes.
EOF
mv $T/termux/* $T/ && rmdir $T/termux
(cd $T && tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -cf $W/sources/sources.tar .)
rm -rf $T
ls -l $W/sources/sources.tar
