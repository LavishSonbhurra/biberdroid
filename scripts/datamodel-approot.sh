#!/bin/bash
# biber started under its own name (not as tectonic-biber), with BIBER_HOME
# set, the way an app runs it for a data model check: libbiber.so
# --validate-datamodel on tests/datamodel, whose refs.bib has planted mistakes
# (a misspelt field, a missing mandatory field, a duplicate key and others).
# biber must exit 0 and log its 5 data model warnings and the duplicate key.
set -e
B=$(cd "$(dirname "$0")/.." && pwd)
W=${BIBER_WORK:-$B/work}
A=$W/scratch/approot
D=/data/data/${APP_ID:-org.example.biberhost}/files/home/datamodel
sudo rm -rf $A$D; sudo mkdir -p $A$D
sudo cp $B/tests/datamodel/main.bcf $B/tests/datamodel/refs.bib $A$D/
sudo chown -R 10234:10234 $A$D
$B/scripts/approot.sh run "cd $D && LD_LIBRARY_PATH=\$NATIVE BIBER_HOME=\$FILES/biber \
  \$NATIVE/libbiber.so --validate-datamodel --nodieonerror main.bcf > main.out 2>&1; echo \$? > status"
fail=0
status=$(sudo cat $A$D/status)
model=$(sudo grep -c 'WARN - Datamodel' $A$D/main.blg || true)
dup=$(sudo grep -c 'WARN - Duplicate entry key' $A$D/main.blg || true)
if [ "$status" = 0 ] && [ "$model" = 5 ] && [ "$dup" = 1 ]; then
  echo "ok   data model check: exit 0, 5 data model warnings, 1 duplicate key"
else
  echo "FAIL data model check: exit $status, $model data model warnings, $dup duplicate keys"
  sudo tail -20 $A$D/main.out
  fail=1
fi
exit $fail
