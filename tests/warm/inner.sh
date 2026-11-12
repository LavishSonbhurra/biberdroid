# Runs inside the app root (scripts/warm-approot.sh), as the app uid, under
# Android's sh: starts warm processes and runs, and leaves what they did in
# files for the host script to judge.
#   inner.sh fixtures | fresh | served | life | terminal | hold | run NAME ADDRESS [WAIT] | soak
export LD_LIBRARY_PATH=$NATIVE PATH=/system/bin:$FILES/biber/bin
W=$FILES/home/warm
L=$NATIVE/libbiber.so
cd $W

# warm_start NAME [VAR=VALUE...]: a warm process at @NAME with this shell's
# environment (plus the variables given), ended by warm_stop or with the shell.
warm_start() {
  local name=$1; shift
  rm -f $W/ctl; mkfifo $W/ctl
  env "$@" BIBER_WARM=${ADDRESS:-@$name} $L --biberdroid-warm < $W/ctl > $W/warm-$name.out 2>&1 &
  WARM=$!
  exec 9> $W/ctl
  local i=0
  while ! grep -q ready $W/warm-$name.out && [ $i -lt 200 ] && kill -0 $WARM 2>/dev/null; do sleep 0.05; i=$((i + 1)); done
}
warm_stop() { exec 9>&-; wait $WARM 2>/dev/null; }
# The launcher processes alive in this root (the host's other processes show in /proc too):
# the warm process, its workers, and runs.
alive() {
  local n=0 p
  for p in $(ps -A -o PID,NAME | grep -e libbiber -e tectonic-biber | while read p name; do echo $p; done); do
    [ "$(readlink /proc/$p/exe 2>/dev/null)" = "$L" ] && n=$((n + 1))
  done
  echo $n
}
worker() { ps -A -o PID,PPID | while read p pp; do [ "$pp" = "$WARM" ] && echo $p; done | head -1; }
settle() { local i=0; while [ "$(alive)" != "$1" ] && [ $i -lt 60 ]; do sleep 0.05; i=$((i + 1)); done; alive; }

case $1 in
fixtures)
  # biber itself: every fixture through the warm process, then all at once with the result cache.
  export BIBER_HOME=$FILES/biber BIBER_TIMING=$W/timing-fixtures.txt TMPDIR=$W BIBER_WARM=@biberdroid-fixtures
  for n in $NAMES; do (cd .tmpf-$n && tectonic-biber $n > ../$n.fresh.out 2> ../$n.fresh.err; echo $? > ../$n.fresh.status); done
  warm_start biberdroid-fixtures
  for n in $NAMES; do (cd .tmpa-$n && tectonic-biber $n > ../$n.served.out 2> ../$n.served.err; echo $? > ../$n.served.status); done
  cp $BIBER_TIMING $W/timing-fixtures-first.txt
  pids=
  for n in $NAMES; do (cd .tmpb-$n && BIBER_CACHE=$W/cache tectonic-biber $n > /dev/null 2>&1) & pids="$pids $!"; done
  wait $pids
  (cd .tmpbroken && tectonic-biber broken > ../broken.served.out 2>&1; echo $? > ../broken.served.status)
  # Whoever reads a run's output to its end (Tectonic does) must not wait for the warm process.
  (cd .tmpc-t1-numeric && tectonic-biber t1-numeric | cat > ../piped.out; echo done > ../piped.status) &
  i=0; while [ ! -e piped.status ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
  alive > alive-busy.txt
  warm_stop
  settle 0 > alive-after.txt
  ;;

fresh|served)
  # The probe in place of biber: each case once on its own (fresh) and once by a warm process (served).
  export BIBER_HOME=$FILES/probe BIBER_TIMING=$W/timing.txt BIBER_WARM=@biberdroid-probe
  rm -f $BIBER_TIMING
  [ $1 = served ] && warm_start biberdroid-probe
  O=$W/$1; mkdir -p $O "$W/a folder" $W/readonly; chmod 555 $W/readonly
  # try NAME COMMAND...: the command's output and status, in the case's own files.
  try() { local name=$1; shift; ("$@") > $O/$name.out 2> $O/$name.err; echo $? > $O/$name.status; }
  cd "$W/a folder"
  try plain $L state end one "two words" "" 'four=4'
  try umask sh -c "umask 027; rm -f made; $L state write=made"
  try nice nice -n 7 $L state
  try limits sh -c "ulimit -n 123; ulimit -c 0; $L state"
  try ignored sh -c "trap '' PIPE HUP USR1; $L state"
  try stdin sh -c "echo some input | $L state stdin"
  try nullin sh -c "$L state stdin < /dev/null"
  try order sh -c "$L order > $O/order.both 2>&1"
  try exit7 $L exit=7 end
  try dies $L write=/nonexistent/file end
  try segv $L kill=SEGV end
  try term $L kill=TERM end
  try int $L kill=INT end
  try hup $L kill=HUP end
  try pipe $L kill=PIPE end
  try usr1 $L kill=USR1 end
  try closed-reader sh -c "{ $L flood end; echo \$? > $O/closed-reader.inner; } | head -c 1 > /dev/null"
  try closed-stdout sh -c "$L state lines=3 >&-"
  try closed-stdin sh -c "$L state stdin <&-"
  try readonly sh -c "cd $W/readonly && $L state write=file"
  try fulldisk sh -c "cd $W/full && $L fill=big; rm -f big"
  try gone sh -c "mkdir $W/gone && cd $W/gone && rmdir $W/gone && $L state"
  try by-name $FILES/probe/bin/tectonic-biber state
  # Another environment than the warm process's: an extra variable, another value, one missing.
  try env-extra env EXTRA=1 $L state
  try env-changed env HOME=/elsewhere $L state
  try env-missing sh -c "unset ANDROID_DATA; $L state"
  # Signals sent to the run from outside, while it works.
  for s in TERM INT HUP QUIT USR1 KILL; do
    try sent-$s sh -c "$L sleep=20 end & p=\$!; sleep 0.7; kill -$s \$p; wait \$p; echo status \$?"
  done
  try sent-ignored sh -c "trap '' TERM; $L sleep=1.5 end & p=\$!; sleep 0.5; kill -TERM \$p; wait \$p; echo status \$?"
  try rand1 $L rand
  try rand2 $L rand
  cp $BIBER_TIMING $W/timing-$1.txt
  if [ $1 = served ]; then
    alive > $W/alive-probe.txt
    ls /proc/$WARM/task | wc -l > $W/threads-probe.txt
    grep VmRSS /proc/$WARM/status > $W/rss-probe.txt
    warm_stop
  fi
  ;;

life)
  # How a warm process starts and ends, and what its runs see when it does.
  export BIBER_HOME=$FILES/probe BIBER_TIMING=$W/timing-life.txt BIBER_WARM=@biberdroid-life
  O=$W/life; mkdir -p $O; cd $O
  note() { echo "$1: $2" >> $O/notes.txt; }
  took() { grep -c 'warm: taken' $BIBER_TIMING; }

  warm_start biberdroid-life
  # The run killed: its worker goes with it.
  $L sleep=30 & p=$!; sleep 0.5; kill -KILL $p; wait $p 2>/dev/null
  note killed-run-leaves "$(settle 1)"
  # The worker killed: the run ends as killed.
  $L sleep=30 & p=$!; sleep 0.5; kill -KILL $(worker); wait $p; note killed-worker-status $?
  # A second warm process at the same address does not start, and does not disturb the first.
  $L --biberdroid-warm < /dev/null > second.out 2>&1; note second-status $?
  $L lines=1 > /dev/null; note after-second-status $?
  # Asked to stop during a run (SIGTERM): the run finishes, then the warm process ends.
  $L sleep=1.5 lines=2 write=term.txt > term.out & p=$!; sleep 0.5; kill -TERM $WARM; wait $p; note term-run-status $?
  note term-leaves "$(settle 0)"
  warm_stop

  # Its standard input closed during a run: the same.
  warm_start biberdroid-life
  $L sleep=1.5 lines=2 > eof.out & p=$!; sleep 0.5; exec 9>&-; wait $p; note eof-run-status $?
  note eof-leaves "$(settle 0)"

  # Killed during a run: the run fails, and what was done is not done again.
  warm_start biberdroid-life
  $L write=once.txt sleep=30 2> killed.err & p=$!; sleep 0.5; kill -KILL $WARM; wait $p; note killed-warm-status $?
  note killed-warm-leaves "$(settle 0)"
  warm_stop
  # Killed while idle: the next run is done on its own.
  warm_start biberdroid-life
  kill -KILL $WARM; wait $WARM 2>/dev/null
  before=$(took); $L lines=1 > idle-killed.out; note idle-killed-status $?; note idle-killed-taken $(($(took) - before))
  warm_stop

  # Stopped (not answering): the run gives up waiting and works on its own, once.
  warm_start biberdroid-life
  kill -STOP $WARM
  BIBER_WARM_WAIT=300 $L write=stalled.txt lines=1 > stalled.out; note stalled-status $?
  kill -CONT $WARM; sleep 0.5
  $L lines=1 > /dev/null; note after-stall-status $?
  warm_stop

  # Idle for long enough: it ends, but not under a run.
  warm_start biberdroid-life BIBER_WARM_IDLE=1
  $L sleep=2 lines=1 > idle.out; note idle-run-status $?
  note idle-right-after "$(alive)"
  sleep 1.6; note idle-later "$(alive)"
  warm_stop

  # A path is not an address (only names in the abstract namespace are).
  BIBER_WARM=$O/warm.sock $L --biberdroid-warm < /dev/null > path.out 2>&1; note path-status $?

  # Its standard input ends while it still compiles (biber itself here): the address is free at
  # once, and a successor started right away takes the runs.
  export BIBER_HOME=$FILES/biber
  rm -f $W/ctl; mkfifo $W/ctl
  $L --biberdroid-warm < $W/ctl > $W/warm-cut.out 2>&1 &
  cut=$!
  exec 9> $W/ctl
  sleep 0.15
  exec 9>&-
  sleep 0.05
  warm_start biberdroid-life
  note successor-ready "$(grep -c ready $W/warm-biberdroid-life.out)"
  before=$(took); $L --version > successor.out; note successor-taken $(($(took) - before))
  wait $cut; note cut-status $?
  note cut-said "$(grep -c . $W/warm-cut.out)"
  warm_stop
  export BIBER_HOME=$FILES/probe

  # A relative PERL5LIB entry would mean another folder in a worker: no warm process, and no run handed over.
  PERL5LIB=lib $L --biberdroid-warm < /dev/null > perl5lib.out 2>&1; note perl5lib-status $?
  warm_start biberdroid-life
  before=$(took); PERL5LIB=lib $L lines=1 > /dev/null; note perl5lib-run-status $?; note perl5lib-taken $(($(took) - before))
  warm_stop


  # A run that comes while the warm process still compiles (biber itself here) is taken once it is ready.
  export BIBER_HOME=$FILES/biber
  rm -f $W/ctl; mkfifo $W/ctl
  $L --biberdroid-warm < $W/ctl > $W/warm-early.out 2>&1 &
  WARM=$!
  exec 9> $W/ctl
  sleep 0.15
  before=$(took); $L --version > early.out; note early-status $?; note early-taken $(($(took) - before))
  grep 'warm: taken' $BIBER_TIMING | tail -1 > early.timing
  warm_stop
  export BIBER_HOME=$FILES/probe

  # A worker that cannot become the run (here: the run's limit on open files is above the warm
  # process's hard limit) leaves the run to itself.
  rm -f $W/ctl; mkfifo $W/ctl
  (ulimit -n 200; exec $L --biberdroid-warm) < $W/ctl > $W/warm-limited.out 2>&1 &
  WARM=$!
  exec 9> $W/ctl
  sleep 0.5
  before=$(took); $L lines=1 write=limited.txt > limited.out; note limited-status $?; note limited-taken $(($(took) - before))
  grep 'not used' $BIBER_TIMING | tail -1 > limited.timing
  warm_stop
  # Its standard input already at its end: it does not even compile.
  $L --biberdroid-warm < /dev/null > noinput.out 2>&1; note noinput-status $?

  # No version marker, or paths that are not absolute: no warm process.
  BIBER_HOME=$FILES/nomarker $L --biberdroid-warm < /dev/null > nomarker.out 2>&1; note nomarker-status $?
  (cd $FILES && BIBER_HOME=probe $L --biberdroid-warm < /dev/null > relative.out 2>&1; note relative-status $?)
  # The Perl tree replaced under a warm process: its runs work on their own.
  warm_start biberdroid-life
  cp $FILES/probe/.version $O/version.keep; echo another-tree > $FILES/probe/.version
  before=$(took); $L lines=1 > /dev/null; note replaced-status $?; note replaced-taken $(($(took) - before))
  cp $O/version.keep $FILES/probe/.version
  warm_stop
  note end-leaves "$(settle 0)"
  ;;
terminal)
  # On a terminal (the host script gives this shell one), perl sends each line out as it is written.
  export BIBER_HOME=$FILES/probe BIBER_TIMING=$W/timing-terminal.txt BIBER_WARM=@biberdroid-terminal
  echo "on its own:"; $L order
  warm_start biberdroid-terminal
  echo "by the warm process:"; $L order
  warm_stop
  ;;

hold)
  # A warm process that stays until the host script has done with it (warm-peers-approot.sh).
  export BIBER_HOME=$FILES/probe BIBER_TIMING=$W/timing-peers.txt BIBER_WARM=@biberdroid-peers
  warm_start biberdroid-peers
  ls /proc/$WARM/fd | wc -l > $W/fds-before.txt
  echo $WARM > $W/hold.pid
  i=0; while [ ! -e $W/hold.stop ] && [ $i -lt 600 ]; do sleep 0.1; i=$((i + 1)); done
  ls /proc/$WARM/fd | wc -l > $W/fds-after.txt
  alive > $W/alive-hold.txt
  warm_stop
  ;;

run)
  # One run of the probe for the process at ADDRESS; the timing file says who did it.
  export BIBER_HOME=$FILES/probe BIBER_TIMING=$W/peers/$2.timing BIBER_WARM=$3
  [ -n "$4" ] && export BIBER_WARM_WAIT=$4
  $L lines=1 write=$W/peers/$2.txt > $W/peers/$2.out 2>&1; echo $? > $W/peers/$2.status
  ;;

run-signalled)
  # As run, with SIGTERM sent to the run while it waits.
  export BIBER_HOME=$FILES/probe BIBER_TIMING=$W/peers/$2.timing BIBER_WARM=$3
  $L lines=1 write=$W/peers/$2.txt sleep=5 > $W/peers/$2.out 2>&1 &
  p=$!; sleep 0.7; kill -TERM $p; wait $p; echo $? > $W/peers/$2.status
  ;;

soak)
  # Many runs, at once and in a row: every result right, and the warm process no bigger after.
  size() { echo "$(ls /proc/$WARM/fd | wc -l) $(sed -n 's/^VmRSS:[^0-9]*\([0-9]*\).*/\1/p' /proc/$WARM/status)"; }
  zombies() { ps -A -o PPID,S | grep -c "^ *$WARM  *Z"; }
  export BIBER_HOME=$FILES/biber BIBER_TIMING=$W/timing-soak.txt BIBER_WARM=@biberdroid-soak
  warm_start biberdroid-soak
  $L --version > /dev/null
  size > $W/soak-biber-before.txt
  bad=0 round=0
  while [ $round -lt $ROUNDS ]; do
    round=$((round + 1)); pids=
    for j in $SLOTS; do (cd $W/s-$j && $L t1-numeric > /dev/null 2>&1) & pids="$pids $!"; done
    wait $pids
    for j in $SLOTS; do cmp -s $W/s-$j/t1-numeric.bbl $W/reference.bbl || bad=$((bad + 1)); rm -f $W/s-$j/t1-numeric.bbl; done
  done
  i=0
  while [ $i -lt $ROW ]; do
    i=$((i + 1))
    (cd $W/s-1 && $L t1-numeric > /dev/null 2>&1) || bad=$((bad + 1))
    cmp -s $W/s-1/t1-numeric.bbl $W/reference.bbl || bad=$((bad + 1)); rm -f $W/s-1/t1-numeric.bbl
  done
  echo $bad > $W/soak-bad.txt
  size > $W/soak-biber-after.txt
  zombies > $W/soak-zombies.txt
  warm_stop
  export BIBER_HOME=$FILES/probe BIBER_TIMING=$W/timing-soak-probe.txt
  warm_start biberdroid-soak
  $L lines=1 > /dev/null
  size > $W/soak-probe-before.txt
  i=0 bad=0
  while [ $i -lt $PROBES ]; do
    i=$((i + 1))
    out=$($L lines=2 exit=3); [ "$?/$out" = "3/line 1
line 2" ] || bad=$((bad + 1))
  done
  echo $bad > $W/soak-probe-bad.txt
  size > $W/soak-probe-after.txt
  zombies >> $W/soak-zombies.txt
  alive > $W/soak-alive.txt
  warm_stop
  settle 0 > $W/soak-left.txt
  ;;
esac
