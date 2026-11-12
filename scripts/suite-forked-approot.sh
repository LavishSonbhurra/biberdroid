#!/bin/bash
# Run biber's own test suite (51 files, 1264 tests) in the minimal app root as
# a warm process runs biber: one process compiles the modules bin/biber loads,
# and each test file runs in a child forked from it (tests/warm/forked-suite.pl).
# Needs the test-only additions scripts/suite-approot.sh puts into the root;
# run that first.
set -e
B=$(cd "$(dirname "$0")/.." && pwd)
W=${BIBER_WORK:-$B/work}   # downloads, the rootfs and build output (gitignored)
A=$W/scratch/approot
F=/data/data/${APP_ID:-org.example.biberhost}/files
sudo test -e $A$F/testlib/perl || { echo "run scripts/suite-approot.sh first"; exit 1; }
sudo cp $B/tests/warm/forked-suite.pl $A$F/testlib/forked-suite.pl
sudo chown 10234:10234 $A$F/testlib/forked-suite.pl
$B/scripts/approot.sh run "cd $F/home/suite && export PATH=/system/bin:$F/testlib LD_LIBRARY_PATH=\$NATIVE BIBER_NATIVE_DIR=\$NATIVE PERL5LIB=$F/biber/lib:$F/testlib PERL5OPT=-MBiberNative && $F/testlib/perl $F/testlib/forked-suite.pl $F/biber/bin/biber 4 2>&1 | tail -${TAIL:-15}"
