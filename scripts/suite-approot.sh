#!/bin/bash
# Run biber's own test suite (51 files, 1264 tests) against the PACKAGED tree
# in the minimal app root. Test-only additions, never shipped: Termux's tiny
# perl executable (needs only libperl.so), the test harness modules and the
# biber source tree's t/ and testfiles/.
set -e
B=$(cd "$(dirname "$0")/.." && pwd)
W=${BIBER_WORK:-$B/work}   # downloads, the rootfs and build output (gitignored)
A=$W/scratch/approot
T=$W/rootfs/data/data/com.termux/files
F=/data/data/${APP_ID:-org.example.biberhost}/files
CORE=$T/usr/lib/perl5/5.44.0
sudo rm -rf $A$F/testlib $A$F/home/suite
sudo mkdir -p $A$F/testlib $A$F/home/suite
sudo cp $T/usr/bin/perl $A$F/testlib/perl
sudo patchelf --remove-rpath --remove-needed libandroid-utimes.so $A$F/testlib/perl
sudo bash -c "cd $CORE && cp -a --parents App TAP Test Test2 Test2.pm Benchmark.pm $A$F/testlib/ 2>/dev/null || true"
sudo bash -c "cd $T/home/biberlib/lib/perl5 && cp -a --parents Test Test2 Text/Diff Text/Diff.pm Algorithm $A$F/testlib/ 2>/dev/null || true"
sudo bash -c "cd $T/home/biber-2.21 && cp -a t testfiles data $A$F/home/suite/"
sudo chown -R 10234:10234 $A$F/testlib $A$F/home/suite
$B/scripts/approot.sh run "cd $F/home/suite && export PATH=/system/bin:$F/testlib LD_LIBRARY_PATH=\$NATIVE BIBER_NATIVE_DIR=\$NATIVE PERL5LIB=$F/biber/lib:$F/testlib PERL5OPT=-MBiberNative && $F/testlib/perl -MBiberNative -MApp::Prove -e 'my \$a=App::Prove->new; \$a->process_args(qw(-j4 t/)); exit(\$a->run ? 0 : 1)' 2>&1 | tail -${TAIL:-15}"
