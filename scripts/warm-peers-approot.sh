#!/bin/bash
# Checks the launcher's warm process (BIBER_WARM) against peers that are not
# what they should be, and under load, in the minimal app root (approot.sh):
#  - clients that are another user's, send nonsense, too much, too many
#    descriptors, or nothing at all (tests/warm/peer.py): each is dropped, none
#    holds up a real run, none leaves a descriptor behind in the warm process;
#  - something else listening at the address, another user's or one that
#    never answers or answers nonsense: the run works on its own, once;
#  - a soak: ROUNDS x 24 biber runs at once, ROW in a row and PROBES probe
#    runs, every result right, with no descriptor, zombie or memory left over.
set -e
B=$(cd "$(dirname "$0")/.." && pwd)
W=${BIBER_WORK:-$B/work}   # downloads, the rootfs and build output (gitignored)
A=$W/scratch/approot
F=/data/data/${APP_ID:-org.example.biberhost}/files
H=$F/home/warm
ROUNDS=${ROUNDS:-5} ROW=${ROW:-80} PROBES=${PROBES:-300}
slots=$(seq 1 24 | tr '\n' ' ')
sudo rm -rf $A$H $A$F/probe
sudo mkdir -p $A$H/peers $A$F/probe/bin
for j in $slots; do
  sudo mkdir -p $A$H/s-$j; sudo cp $B/tests/fixtures/t1-numeric/t1-numeric.bcf $B/tests/fixtures/t1-numeric/*.bib $A$H/s-$j/
done
sudo cp $B/tests/fixtures/t1-numeric/t1-numeric.bbl $A$H/reference.bbl
sudo cp $B/tests/warm/probe.pl $A$F/probe/bin/biber
sudo ln -s ../biber/lib $A$F/probe/lib
echo probe-tree | sudo tee $A$F/probe/.version >/dev/null
sudo cp $B/tests/warm/inner.sh $A$H/inner.sh
sudo chown -R 10234:10234 $A$H $A$F/probe
inner() { $B/scripts/approot.sh run "sh $H/inner.sh $*" || true; }
as_app() { sudo setpriv --reuid 10234 --regid 10234 --clear-groups python3 -I $B/tests/warm/peer.py "$@"; }
as_other() { python3 -I $B/tests/warm/peer.py "$@"; }
await() { local i=0; while ! sudo test -e "$1" && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done; }
P=$A$H/peers
O=$W/scratch/warm-peers   # what the peers print, on the host
rm -rf $O; mkdir -p $O

inner hold &
await $A$H/hold.pid
as_other client silent @biberdroid-peers > $O/other.txt 2>&1 || true
for k in garbage huge descriptors nothing-in-it slow half silent; do as_app client $k @biberdroid-peers > $O/$k.txt 2>&1 || true; done
as_app client slow @biberdroid-peers > /dev/null 2>&1 &
slow=$!
sleep 0.3; inner run beside-slow @biberdroid-peers
as_app client crowd @biberdroid-peers > /dev/null 2>&1 &
crowd=$!
sleep 0.5; inner run in-crowd @biberdroid-peers 500
wait $slow $crowd 2>/dev/null || true
sleep 2.5; inner run after-crowd @biberdroid-peers
sudo touch $A$H/hold.stop
wait

as_other server silent @biberdroid-fake1 > $O/fake1.txt 2>&1 &
sleep 0.5; inner run other-user @biberdroid-fake1
as_app server silent @biberdroid-fake2 > $O/fake2.txt 2>&1 &
sleep 0.5; inner run mute @biberdroid-fake2 400
as_app server garbage @biberdroid-fake3 > $O/fake3.txt 2>&1 &
sleep 0.5; inner run nonsense @biberdroid-fake3
wait

$B/scripts/approot.sh run "ROUNDS=$ROUNDS ROW=$ROW PROBES=$PROBES SLOTS='$slots' sh $H/inner.sh soak" || true

fail=0
check() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; fail=1; fi; }
has() { sudo grep -q -- "$2" $A$H/$1; }
once() { [ "$(sudo cat $P/$1.status)" = 0 ] && [ "$(sudo grep -c written $P/$1.txt)" = 1 ]; }
first() { sudo cut -d' ' -f1 $A$H/$1; }
second() { sudo cut -d' ' -f2 $A$H/$1; }

check "another user's client is dropped at once" "grep -q 'closed after 0.0 s' $O/other.txt"
for k in garbage huge descriptors nothing-in-it; do
  check "a client sending $k is dropped at once" "grep -q 'closed after 0.[0-9] s' $O/$k.txt"
done
for k in slow half silent; do
  check "a client that stays $k is dropped after two seconds" "grep -q 'closed after [12].[0-9] s' $O/$k.txt"
done
check "a slow client does not hold up a run" "once beside-slow && has peers/beside-slow.timing 'warm: taken after [0-9][0-9]* ms'"
check "a crowd of idle clients: the run is done, once" "once in-crowd"
check "and the warm process serves again when they are gone" "once after-crowd && has peers/after-crowd.timing 'warm: taken'"
check "no descriptor of a peer stays in the warm process" "[ \$(sudo cat $A$H/fds-before.txt) = \$(sudo cat $A$H/fds-after.txt) ]"
check "another user's process at the address is not used" "once other-user && has peers/other-user.timing 'is not this user'"
check "a process that never answers is not waited for" "once mute && has peers/mute.timing 'did not take the run in time'"
check "a process that answers nonsense is not used" "once nonsense && has peers/nonsense.timing 'warm process not used: refused'"

runs=$((ROUNDS * 24 + ROW))
check "soak: $runs biber runs, every .bbl identical to the official biber's" "[ \$(sudo cat $A$H/soak-bad.txt) = 0 ]"
check "soak: at least 16 of each 24 at once were done by the warm process" "[ \$(sudo grep -c 'warm: taken' $A$H/timing-soak.txt) -ge $((ROUNDS * 16 + ROW)) ]"
check "soak: $PROBES probe runs, each with its output and status" "[ \$(sudo cat $A$H/soak-probe-bad.txt) = 0 ] && [ \$(sudo grep -c 'warm: taken' $A$H/timing-soak-probe.txt) -ge $PROBES ]"
for t in biber probe; do
  check "soak, $t: the warm process has no more descriptors open than before" "[ \$(first soak-$t-before.txt) = \$(first soak-$t-after.txt) ]"
  check "soak, $t: and holds no more memory (within 1 MB)" "[ \$((\$(second soak-$t-after.txt) - \$(second soak-$t-before.txt))) -lt 1024 ]"
done
check "soak: no zombie" "[ \"\$(sudo cat $A$H/soak-zombies.txt | tr '\n' ' ')\" = '0 0 ' ]"
check "soak: only the warm process is left, and it ends" "[ \$(sudo cat $A$H/soak-alive.txt) = 1 ] && [ \$(sudo cat $A$H/soak-left.txt) = 0 ]"
echo "memory (descriptors, resident kB) before and after: biber $(sudo cat $A$H/soak-biber-before.txt) -> $(sudo cat $A$H/soak-biber-after.txt), probe $(sudo cat $A$H/soak-probe-before.txt) -> $(sudo cat $A$H/soak-probe-after.txt)"
exit $fail
