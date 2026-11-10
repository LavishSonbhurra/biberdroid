#!/bin/bash
# Makes tests/fixtures: compiles every tests/t*.tex with a glibc Tectonic
# (TECTONIC) and the official biber 2.21 linux-aarch64 binary (REF_BIBER) on
# PATH, against a Tectonic bundle holding biblatex 3.21 (BUNDLE, a URL or a
# folder; the fixtures in the tree were made with a TeX Live 2026 bundle),
# then keeps each .bcf, its .bib files and the reference .bbl. Only needed to
# regenerate the fixtures; they are kept in the tree.
#
# The reference binary is biber-linux-aarch64.tar.gz from biber's SourceForge
# release files:
#   https://sourceforge.net/projects/biblatex-biber/files/biblatex-biber/2.21/binaries/ARM/biber-linux-aarch64.tar.gz/download
#   sha256 ad189e7589490d48cf5dcce9a167899406fdf707321aa6771f6f3d858ac6740c
set -e
B=$(cd "$(dirname "$0")/.." && pwd)
W=${BIBER_WORK:-$B/work}   # downloads, the rootfs and build output (gitignored)
TEC=${TECTONIC:?set TECTONIC to a glibc tectonic binary}
BUNDLE=${BUNDLE:?set BUNDLE to a Tectonic bundle with biblatex 3.21}
REF_BIBER=${REF_BIBER:?set REF_BIBER to the official biber 2.21 binary}
export TECTONIC_CACHE_DIR=$W/scratch/teccache PAR_GLOBAL_TMPDIR=$W/scratch/parcache
mkdir -p $W/scratch/refbin $TECTONIC_CACHE_DIR $PAR_GLOBAL_TMPDIR
ln -sf $REF_BIBER $W/scratch/refbin/biber
export PATH=$W/scratch/refbin:$PATH
for t in $B/tests/t*.tex; do
  n=$(basename $t .tex); d=$W/scratch/ref/$n
  rm -rf $d; mkdir -p $d; cp $B/tests/*.bib $t $d/
  ( cd $d && $TEC -b $BUNDLE \
      --keep-intermediates --keep-logs $n.tex > tectonic.out 2>&1 ) && echo "$n ok" || { echo "$n FAILED"; tail -20 $d/tectonic.out; }
done
for d in $W/scratch/ref/t*/; do
  n=$(basename $d); f=$B/tests/fixtures/$n; mkdir -p $f
  cp $d/$n.bcf $d/$n.bbl $f/
  for b in $(grep -o '>[^<]*\.bib<' $d/$n.bcf | tr -d '<>' | sort -u); do cp $B/tests/$b $f/; done
done
