#!/usr/bin/env python3
"""Times biber runs with and without a warm process in the app root.

  sudo python3 -I bench.py APPROOT NATIVE FILES FIXTURE_DIR [RUNS]

Runs as root to enter the root and become the app uid without a helper
process in the measured time. Prints medians: a run on its own, a run a warm
process did, and how long a warm process takes from its start until it is
ready.
"""
import os
import shutil
import statistics
import subprocess
import sys
import time

root, native, files, fixture = sys.argv[1:5]
runs = int(sys.argv[5]) if len(sys.argv) > 5 else 31
name = os.path.basename(fixture.rstrip("/"))
work = files + "/home/bench"
host_work = root + work.replace("/data/user/0/", "/data/data/")   # the link is absolute
shutil.rmtree(host_work, ignore_errors=True)
os.makedirs(host_work)
for f in os.listdir(fixture):
    if f.endswith((".bcf", ".bib")):
        shutil.copy(os.path.join(fixture, f), host_work)
os.chown(host_work, 10234, 10234)
for f in os.listdir(host_work):
    os.chown(os.path.join(host_work, f), 10234, 10234)

env = {"PATH": "/system/bin", "HOME": files + "/home", "TMPDIR": files + "/home", "ANDROID_ROOT": "/system",
       "ANDROID_DATA": "/data", "LD_LIBRARY_PATH": native, "BIBER_HOME": files + "/biber"}
warm_env = dict(env, BIBER_WARM="@biberdroid-bench")
launcher = native + "/libbiber.so"


def enter():
    os.chroot(root)
    os.chdir(work)
    os.setgroups([])
    os.setgid(10234)
    os.setuid(10234)


def run(environment):
    start = time.perf_counter()
    done = subprocess.run([launcher, name], env=environment, preexec_fn=enter, stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL)
    took = time.perf_counter() - start
    if done.returncode != 0:
        raise SystemExit("biber failed")
    return took * 1000


def warm():
    start = time.perf_counter()
    p = subprocess.Popen([launcher, "--biberdroid-warm"], env=warm_env, preexec_fn=enter, stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    line = p.stdout.readline()
    took = time.perf_counter() - start
    if b"ready" not in line:
        raise SystemExit("no warm process")
    return p, took * 1000


def stop(p):
    p.stdin.close()
    p.wait()


starts = []
for _ in range(7):
    p, took = warm()
    starts.append(took)
    stop(p)
p, _ = warm()
run(env), run(warm_env)
fresh, served = [], []
for _ in range(runs):
    fresh.append(run(env))
    served.append(run(warm_env))
stop(p)
with open(os.path.join(host_work, name + ".bbl"), "rb") as made, open(os.path.join(fixture, name + ".bbl"), "rb") as want:
    if made.read() != want.read():
        raise SystemExit("the .bbl differs")


def line(label, times):
    times = sorted(times)
    print("%-34s median %4.0f ms   (fastest %4.0f, slowest %4.0f, %d runs)"
          % (label, statistics.median(times), times[0], times[-1], len(times)))


line("a run on its own", fresh)
line("a run a warm process did", served)
line("a warm process, start to ready", starts)
