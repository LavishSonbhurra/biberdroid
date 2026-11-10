#!/bin/bash
# Run the packaged biber (approot: no Termux, app uid, ELF only in
# nativeLibraryDir) on every fixture .bcf; diff each .bbl against the one the
# official biber 2.21 linux-aarch64 (glibc) binary wrote (tests/fixtures,
# made by ref-run.sh).
B=$(cd "$(dirname "$0")/.." && pwd)
W=${BIBER_WORK:-$B/work}   # downloads, the rootfs and build output (gitignored)
A=$W/scratch/approot
CMP=/data/data/${APP_ID:-org.example.biberhost}/files/home/cmp
sudo rm -rf $A$CMP; sudo mkdir -p $A$CMP
for d in $B/tests/fixtures/t*/; do
  n=$(basename $d); sudo mkdir -p $A$CMP/$n; sudo cp $d/$n.bcf $d/*.bib $A$CMP/$n/
done
sudo chown -R 10234:10234 $A$CMP
$B/scripts/approot.sh run "for d in $CMP/t*; do cd \$d; n=\${d##*/}; LD_LIBRARY_PATH=\$NATIVE PATH=/system/bin:\$FILES/biber/bin tectonic-biber \$n > ../\$n.out 2>&1; echo \"\$n exit=\$? \$(grep -c WARN ../\$n.out) warnings, \$(grep -c ERROR ../\$n.out) errors\"; done"
fail=0
for d in $B/tests/fixtures/t*/; do
  n=$(basename $d)
  if sudo cmp -s $d/$n.bbl $A$CMP/$n/$n.bbl; then echo "$n: .bbl identical ($(wc -c < $d/$n.bbl) bytes)"
  else echo "$n: .bbl DIFFERS"; sudo diff $d/$n.bbl $A$CMP/$n/$n.bbl | head -20; fail=1; fi
done
exit $fail
