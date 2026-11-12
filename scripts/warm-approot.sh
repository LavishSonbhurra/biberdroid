#!/bin/bash
# Checks the launcher's warm process (BIBER_WARM) in the minimal app root
# (approot.sh). A run a warm process did must be the run it would have been
# on its own:
#  - biber: every fixture's .bbl byte-identical to the official biber's, with
#    the same output and exit status as a run on its own, also seven at once
#    with the result cache;
#  - a probe in biber's place (tests/warm/probe.pl) reports the state it
#    starts in (arguments, folder, environment, umask, priority, limits,
#    signals, open descriptors, buffering to a file and to a terminal) and is
#    made to fail, to die by signals, to write to a closed pipe, a full disk
#    and a read-only folder: all of it identical with and without the warm
#    process;
#  - the warm process ends when told to, by its standard input, a signal or
#    idleness, without cutting a run short; runs survive its absence, its
#    death while idle, a stalled or stale one, and fail without redoing work
#    when it is killed under them.
set -e
B=$(cd "$(dirname "$0")/.." && pwd)
W=${BIBER_WORK:-$B/work}   # downloads, the rootfs and build output (gitignored)
A=$W/scratch/approot
F=/data/data/${APP_ID:-org.example.biberhost}/files
H=$F/home/warm
mountpoint -q $A$H/full && sudo umount $A$H/full
python3 -I $B/tests/warm/withpty.py $B/scripts/approot.sh run "sh $H/inner.sh terminal" > $W/scratch/warm-terminal.txt 2>&1 || true
sudo rm -rf $A$H $A$F/probe $A$F/nomarker
sudo mkdir -p $A$H/cache $A$H/full $A$F/probe/bin $A$F/nomarker/bin
names=
for d in $B/tests/fixtures/t*/; do
  n=$(basename $d); names="$names $n"
  for r in f a b; do
    sudo mkdir -p $A$H/.tmp$r-$n; sudo cp $d/$n.bcf $d/*.bib $A$H/.tmp$r-$n/
  done
done
sudo mkdir -p $A$H/.tmpc-t1-numeric; sudo cp $B/tests/fixtures/t1-numeric/*.bcf $B/tests/fixtures/t1-numeric/*.bib $A$H/.tmpc-t1-numeric/
sudo mkdir -p $A$H/.tmpbroken; echo '<not a control file' | sudo tee $A$H/.tmpbroken/broken.bcf >/dev/null
# The probe's tree: biber's own modules, with the probe as bin/biber.
for t in probe nomarker; do
  sudo cp $B/tests/warm/probe.pl $A$F/$t/bin/biber
  sudo ln -s ../biber/lib $A$F/$t/lib
done
echo probe-tree | sudo tee $A$F/probe/.version >/dev/null
sudo ln -s "$(sudo readlink $A$F/biber/bin/tectonic-biber)" $A$F/probe/bin/tectonic-biber
sudo cp $B/tests/warm/inner.sh $A$H/inner.sh
sudo chown -R 10234:10234 $A$H $A$F/probe $A$F/nomarker
# A folder on a disk that is full after 64 KiB.
sudo mount -t tmpfs -o size=64k,uid=10234,gid=10234,mode=700 none $A$H/full
for part in fixtures fresh served life; do
  $B/scripts/approot.sh run "NAMES='$names' sh $H/inner.sh $part" || true
done
sudo umount $A$H/full
python3 -I $B/tests/warm/withpty.py $B/scripts/approot.sh run "sh $H/inner.sh terminal" > $W/scratch/warm-terminal.txt 2>&1 || true

fail=0
check() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; fail=1; fi; }
has() { sudo grep -q -- "$2" $A$H/$1; }
taken() { sudo grep -c 'warm: taken' $A$H/$1; }
note() { sudo sed -n "s/^$1: //p" $A$H/life/notes.txt | tail -1; }

check "the warm process took the seven runs" "[ \$(taken timing-fixtures-first.txt) = 7 ]"
for n in $names; do
  for r in f a b; do
    check "$n ($r): .bbl identical to the official biber's" "sudo cmp -s $A$H/.tmp$r-$n/$n.bbl $B/tests/fixtures/$n/$n.bbl"
  done
  # biber's messages come in an order that differs between two runs on their own (a hash's).
  for k in out err status; do
    check "$n: same $k as a run on its own" "[ \"\$(sudo sort $A$H/$n.fresh.$k | sha256sum)\" = \"\$(sudo sort $A$H/$n.served.$k | sha256sum)\" ]"
  done
done
check "the .bib copies were removed, as without a warm process" "! sudo test -e $A$H/.tmpa-t2-authoryear/refs.bib"
check "seven results cached" "[ \$(sudo ls $A$H/cache | wc -l) = 7 ]"
check "biber's exit status comes through" "[ \$(sudo cat $A$H/broken.served.status) = 2 ] && has broken.served.out ERROR"
check "a reader of the run's output does not wait for the warm process" "has piped.status done && has piped.out 'Output to'"
check "no worker outlives its run" "[ \$(sudo cat $A$H/alive-busy.txt) = 1 ]"
check "the warm process ends with its standard input" "[ \$(sudo cat $A$H/alive-after.txt) = 0 ]"

cases=$(sudo ls $A$H/fresh | sed -n 's/\.status$//p')
check "the probe ran every case" "[ \$(echo \$cases | wc -w) = 32 ]"
for c in $cases; do
  [ $c = rand1 ] || [ $c = rand2 ] && continue
  same=1
  # Process ids aside (Android's own report of a fatal signal has them).
  for k in out err status; do
    [ "$(sudo sed -E 's/(tid|pid) [0-9]+/\1 N/g' $A$H/fresh/$c.$k | sha256sum)" = "$(sudo sed -E 's/(tid|pid) [0-9]+/\1 N/g' $A$H/served/$c.$k | sha256sum)" ] || same=0
  done
  check "probe, $c: the same with a warm process" "[ $same = 1 ]"
done
# 32 cases: two with a closed descriptor, and five ended by a signal, which leave no timing line.
check "the warm process took every case it can take" "[ \$(taken timing-served.txt) = 25 ] && [ \$(taken timing-fresh.txt) = 0 ]"
check "a closed standard descriptor is left to the run itself" "[ \$(sudo grep -c 'a standard descriptor is closed' $A$H/timing-served.txt) = 2 ]"
check "the report has the run's state" "has served/plain.out 'args: state|end|one|two words||four=4' && has served/plain.out 'cwd: .*/a folder' && has served/plain.out 'started: now'"
check "the umask is the run's" "has served/umask.out 'umask: 0027' && has served/umask.out 'mode: 0640'"
check "the priority is the run's" "has served/nice.out 'nice: 7'"
check "the limits are the run's" "has served/limits.out 'Max open files  *123  *123'"
check "ignored signals stay ignored, the others are perl's" "has served/ignored.out 'sig PIPE: IGNORE' && has served/ignored.out 'sig TERM: CODE' && has served/plain.out 'sig PIPE: CODE'"
check "standard input is the run's" "has served/stdin.out 'stdin: 11 bytes' && has served/nullin.out 'stdin: 0 bytes'"
check "standard output is buffered as in a run" "[ \"\$(sudo head -1 $A$H/served/order.both)\" = 'then on stderr' ]"
check "and line by line on a terminal, as in a run" "[ \"\$(grep -A2 'by the warm process' $W/scratch/warm-terminal.txt | tr '\n' '/')\" = 'by the warm process:/first on stdout/then on stderr/' ] && [ \$(taken timing-terminal.txt) = 1 ]"
check "nothing of the warm process is open in a run" "! has served/plain.out '^open: socket' && ! has served/plain.out '^open: pipe'"
check "a signal perl traps ends the run as perl ends it" "has served/term.err 'Caught a SIGTERM' && has served/term.err 'END block ran' && has served/pipe.err 'Caught a SIGPIPE'"
check "a reader that went away ends the run by SIGPIPE's handler" "has served/closed-reader.err 'Caught a SIGPIPE' && has served/closed-reader.err 'END block ran'"
check "a signal sent to the run reaches biber" "has served/sent-TERM.err 'Caught a SIGTERM' && has served/sent-TERM.err 'END block ran' && has served/sent-HUP.err 'Caught a SIGHUP'"
check "a run killed by a signal ends by that signal" "has served/sent-USR1.out 'status 138' && has served/sent-KILL.out 'status 137' && [ \$(sudo cat $A$H/served/segv.status) = 139 ]"
check "a full disk and a read-only folder fail as in a run" "has served/fulldisk.err 'No space left' && has served/readonly.err 'cannot write file'"
check "each run has its own random numbers" "! sudo cmp -s $A$H/served/rand1.out $A$H/served/rand2.out"
check "only the warm process is left after the probe's cases" "[ \$(sudo cat $A$H/alive-probe.txt) = 1 ]"

check "a killed run takes its worker along" "[ \"\$(note killed-run-leaves)\" = 1 ]"
check "a killed worker ends its run as killed" "[ \"\$(note killed-worker-status)\" = 137 ]"
check "a second warm process at the address does not start" "[ \"\$(note second-status)\" = 2 ] && has life/second.out 'cannot listen' && [ \"\$(note after-second-status)\" = 0 ]"
check "SIGTERM lets the run finish, then ends the warm process" "[ \"\$(note term-run-status)\" = 0 ] && has life/term.out 'line 2' && [ \"\$(note term-leaves)\" = 0 ]"
check "so does the end of its standard input" "[ \"\$(note eof-run-status)\" = 0 ] && has life/eof.out 'line 2' && [ \"\$(note eof-leaves)\" = 0 ]"
check "killed under a run: the run fails" "[ \"\$(note killed-warm-status)\" = 137 ] && has life/killed.err 'the warm process ended during this run' && [ \"\$(note killed-warm-leaves)\" = 0 ]"
check "and its work is not done again" "[ \$(sudo grep -c written $A$H/life/once.txt) = 1 ]"
check "killed while idle: the next run works on its own" "[ \"\$(note idle-killed-status)\" = 0 ] && [ \"\$(note idle-killed-taken)\" = 0 ] && has life/idle-killed.out 'line 1'"
check "a warm process that does not answer: the run works on its own, once" "[ \"\$(note stalled-status)\" = 0 ] && [ \$(sudo grep -c written $A$H/life/stalled.txt) = 1 ] && [ \"\$(note after-stall-status)\" = 0 ]"
check "an idle warm process ends, but not under a run" "[ \"\$(note idle-run-status)\" = 0 ] && [ \"\$(note idle-right-after)\" = 1 ] && [ \"\$(note idle-later)\" = 0 ]"
check "a socket file works as an address, for its owner only, and is removed" "[ \"\$(note path-taken)\" = 1 ] && [ \"\$(note path-mode)\" = 600 ] && [ \"\$(note path-after-stop)\" = 0 ]"
check "a stale socket file: runs work on their own, a new warm process leaves it alone" "[ \"\$(note stale-run-status)\" = 0 ] && [ \"\$(note stale-start-status)\" = 2 ] && [ \"\$(note stale-file-kept)\" = 1 ]"
check "a run that comes while the warm process compiles is taken when it is ready" "[ \"\$(note early-status)\" = 0 ] && [ \"\$(note early-taken)\" = 1 ] && has life/early.out 'biber version' && has life/early.timing 'taken after [0-9][0-9][0-9]* ms'"
check "a worker that cannot take on the run's limits leaves the run to itself, once" "[ \"\$(note limited-status)\" = 0 ] && [ \"\$(note limited-taken)\" = 0 ] && has life/limited.timing 'refused: setrlimit' && [ \$(sudo grep -c written $A$H/life/limited.txt) = 1 ]"
check "a warm process whose standard input has ended does not start" "[ \"\$(note noinput-status)\" = 0 ] && ! sudo test -s $A$H/life/noinput.out"
check "no warm process without a version marker or with relative paths" "[ \"\$(note nomarker-status)\" = 2 ] && [ \"\$(note relative-status)\" = 2 ]"
check "a replaced Perl tree is not served by the old warm process" "[ \"\$(note replaced-status)\" = 0 ] && [ \"\$(note replaced-taken)\" = 0 ]"
check "no process is left" "[ \"\$(note end-leaves)\" = 0 ]"
exit $fail
