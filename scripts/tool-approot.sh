#!/bin/bash
# Checks the launcher's Tectonic mode in the minimal app root (approot.sh).
# Run as tectonic-biber in a .tmp* folder directly inside TMPDIR holding the
# .bcf and its data files, as Tectonic's temporary folder does, it must remove
# the .bib copies (Tectonic would write them back into the document's folder)
# and nothing else; anywhere else it must delete nothing. With BIBER_CACHE, the
# same input must reuse the earlier result without starting perl, a changed
# .bib must run biber again, a broken cache entry must be replaced, and an
# unfinished result left by a killed run must be cleared once it is old.
set -e
B=$(cd "$(dirname "$0")/.." && pwd)
W=${BIBER_WORK:-$B/work}   # downloads, the rootfs and build output (gitignored)
A=$W/scratch/approot
F=/data/data/${APP_ID:-org.example.biberhost}/files
H=$F/home/tool
n=t1-numeric
sudo rm -rf $A$H; sudo mkdir -p $A$H/cache/.new-99999
sudo touch -d '2 hours ago' $A$H/cache/.new-99999
for r in 1 2 3 4 project; do
  d=$A$H/.tmprun$r; [ $r = project ] && d=$A$H/project
  sudo mkdir -p $d; sudo cp $B/tests/fixtures/$n/* $d/; sudo rm $d/$n.bbl
  echo notes | sudo tee $d/notes.txt >/dev/null
done
sudo sed -i 's/Addison-Wesley/Addison Wesley/' $A$H/.tmprun4/refs.bib
sudo chown -R 10234:10234 $A$H
$B/scripts/approot.sh run "export LD_LIBRARY_PATH=\$NATIVE PATH=/system/bin:\$FILES/biber/bin BIBER_CACHE=$H/cache TMPDIR=$H
  go() { cd \$1; tectonic-biber $n > $H/\$2.out 2>&1; echo \"\$2 exit=\$? left: \$(ls | tr '\n' ' ')\"; }
  go $H/.tmprun1 run1
  for e in $H/cache/[0-9a-f]*; do rm -f \$e/*; done   # a broken entry
  go $H/.tmprun2 run2; go $H/.tmprun3 run3; go $H/.tmprun4 run4; go $H/project project
  echo \"cache: \$(ls -A $H/cache | tr '\n' ' ')\""
fail=0
check() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; fail=1; fi; }
reused() { sudo grep -q "result reused" $A$H/$1.out; }
check "run1: .bbl identical to the official biber's" "sudo cmp -s $A$H/.tmprun1/$n.bbl $B/tests/fixtures/$n/$n.bbl"
check "run2: a broken cache entry is not used" "! reused run2"
check "run3: the same input reuses the result" "reused run3"
check "run3: .bbl identical to run1" "sudo cmp -s $A$H/.tmprun3/$n.bbl $A$H/.tmprun1/$n.bbl"
check "run4: a changed .bib runs biber" "! reused run4 && sudo grep -q 'Addison Wesley' $A$H/.tmprun4/$n.bbl"
for r in 1 2 3 4; do
  check "run$r: only the .bib copies were removed" \
    "[ \"\$(sudo ls $A$H/.tmprun$r | tr '\n' ' ')\" = 'notes.txt $n.bbl $n.bcf $n.blg ' ]"
done
check "outside a Tectonic folder nothing is removed" "sudo test -f $A$H/project/refs.bib -a -f $A$H/project/notes.txt -a -f $A$H/project/$n.bcf"
check "an old unfinished result is cleared" "! sudo test -e $A$H/cache/.new-99999"
check "two results kept" "[ \$(sudo ls $A$H/cache | wc -l) = 2 ]"
exit $fail
