# Building biberdroid

- [Host](#host)
- [Steps](#steps)
- [What is shipped and how it is made](#what-is-shipped-and-how-it-is-made)
- [Checks](#checks)
- [The Android library](#the-android-library)
- [The warm process](#the-warm-process)
- [Using the payload directly](#using-the-payload-directly)
- [Pins and the download mirror](#pins-and-the-download-mirror)
- [Regenerating the fixtures](#regenerating-the-fixtures)
- [Repository layout](#repository-layout)

## Host

An aarch64 Linux machine with sudo, curl, python3, patchelf, tar, gzip and git. GitHub's
`ubuntu-24.04-arm` runners qualify. The build needs about 2 GB of disk. From a clean work
folder with the downloads already in place, it took 8 minutes on a 2-core Ampere Altra VM,
checks included.

An x86_64 host would need qemu-user registered with binfmt_misc (with the F flag) for the
chroot. That has not been tried and would be much slower.

## Steps

`./build.sh` runs these in order; name steps to run only those (`./build.sh package verify`).
Everything goes to `work/`, or to the folder in `BIBER_WORK`.

| Step | What it does |
| --- | --- |
| `fetch` | Downloads every pinned file into `work/dl/pinned` and checks its SHA-256. A file that fails upstream is taken from the mirror release instead. |
| `rootfs` | Unpacks termux-docker's root filesystem into `work/rootfs`. |
| `packages` | Inside the chroot: Termux's build tools from apt (clang, make and friends; not shipped, not pinned), then the pinned `perl`, `libxml2`, `zlib` and `libiconv` packages with dpkg. |
| `cpan` | The 112 pinned CPAN distributions, in dependency order, with `cpanm --notest` and no network, so an unpinned dependency fails the build. |
| `biber` | biber's `Build.PL`, `./Build test` (1264 tests, on bionic) and `./Build install`. |
| `launcher` | Compiles `src/biber-launcher.c` into `libbiber.so` with Termux's clang. |
| `package` | `scripts/package.py` assembles `work/out`, `scripts/licenses.py` attributes every file to its component, `scripts/notices.py` writes `NOTICES.txt`, `scripts/check-elf.py` checks the libraries, and `scripts/archive.py` packs the Perl tree. |
| `verify` | The checks below, in an Android-like root. |
| `dist` | The release files in `work/dist`, with `SHA256SUMS`. |
| `sources` | Not run by default. The complete source of the payload as one tar (`scripts/sources.sh`); `dist` adds it when present. |
| `umount` | Not run by default. Unmounts the `/proc`, `/sys` and `/dev` binds of the chroots. |

The chroot steps bind-mount the host's `/proc`, `/sys` and `/dev` into `work/rootfs` and
`work/scratch/approot`. Run `./build.sh umount` before deleting `work/`.

The build enters the chroot with `sudo chroot` and runs as Termux's unprivileged user
(`scripts/tx.sh`), the way termux-docker's own entry point does. This works because the Linux
kernel runs Android's bionic binaries directly, given Android's `/system` (here the Android 9
one inside termux-docker). Nothing from that `/system` ships.

## What is shipped and how it is made

`work/out` after `package`:

| Path | Contents |
| --- | --- |
| `jniLibs/arm64-v8a/libbiber.so` | The launcher. It embeds Perl and runs `bin/biber`. |
| `jniLibs/arm64-v8a/libperl.so`, `libxml216.so`, `libz1.so`, `libiconv.so` | Termux's builds. |
| `jniLibs/arm64-v8a/libpl_<Module>.so` | The 41 XS modules biber loads, from CPAN and Perl's core. |
| `assets/biber/` | `bin/biber` and `lib/`: biber, the pure-Perl modules and the parts of Perl's core library it needs. |
| `perl-tree.tar.xz`, `perl-tree.sha256` | `assets/biber` as a reproducible ustar in xz, and its SHA-256. |
| `manifest.tsv`, `FILES.tsv`, `COMPONENTS.tsv`, `NOTICES.txt` | Every shipped file, where it came from, and the licences. |

No source of biber, Perl or any CPAN module is changed (`src/BiberNative.pm`, described
below, answers one ExtUtils::MakeMaker method in its own file). The ELF files from Termux are
changed only with patchelf:

- `RUNPATH` removed.
- Versioned names made APK-safe: `libxml2.so.16` becomes `libxml216.so` and `libz.so.1`
  becomes `libz1.so`, with `NEEDED` entries and sonames to match. Android only installs files
  named `lib*.so`.
- `libandroid-utimes.so` dropped from `NEEDED`. It provides `futimes` and `lutimes`, which
  bionic has had since API 26; that is why the payload needs API 26. It is also the only
  Termux library aligned for 4 KB pages, so it would not load on a 16 KB device.

The Perl tree holds only what biber uses. Left out: the network modules (remote data
sources), XML::LibXSLT, the build and test tools, the extra CJK encodings, and DateTime's
per-locale data and named time zones. The parts of Perl's core library come from what biber's
test suite and the test documents opened (`scripts/core-files.txt`), plus data Perl reads by
name at run time: every collation locale, charnames and Encode's modules.
`scripts/xs-files.txt` lists the XS modules the same way.

`src/BiberNative.pm` replaces `XSLoader::load` and `DynaLoader::bootstrap`: module `Foo::Bar`
is loaded from `$BIBER_NATIVE_DIR/libpl_Foo_Bar.so`, and Perl's usual lookup is the fallback.
It also drops Termux's prefix from `@INC`, and answers the one question biber's start-up puts
to ExtUtils::MakeMaker (is this file a command, for its `kpsewhich` lookup) itself, with
MakeMaker's own test, so that MakeMaker is not loaded for it (0.05 s of every run).

The launcher's environment:

| Variable | Meaning | Default |
| --- | --- | --- |
| `BIBER_HOME` | The Perl tree. | The folder above the one holding `argv[0]`, so a link at `BIBER_HOME/bin/tectonic-biber` needs no variable. |
| `BIBER_NATIVE_DIR` | Where the `libpl_*.so` files are. | The folder of `/proc/self/exe`, which is `nativeLibraryDir`. |
| `BIBER_CACHE` | Optional folder for reusing results in Tectonic mode. | None. |
| `BIBER_WARM` | Optional address of [a warm process](#the-warm-process) that does the run: `@name`. | None. |
| `BIBER_WARM_WAIT` | Milliseconds a run waits for the warm process to take it before it runs on its own. | 3000 |
| `BIBER_WARM_IDLE` | For the warm process: seconds without a run after which it ends; 0 for no limit. | 0 |
| `BIBER_TIMING` | `1` prints one line on stderr saying where the run's time went (`start`, `compile`, `run`, `exit` and `total`, in milliseconds; for a run a warm process did, `start`, `warm: taken after`, `run` and `total`, and for one it did not take, a line before that saying why); an absolute path appends the lines to that file. | None. |
| `LD_LIBRARY_PATH` | Must hold `nativeLibraryDir`, for `libperl.so`. | |

Started under the name `tectonic-biber`, the launcher acts as Tectonic's external tool. After
the run it removes the data files named in the `.bcf` that were in the folder before biber ran
(Tectonic's copies, which it would otherwise write back into the document's folder), and only
when the folder is a `.tmp*` folder directly inside `TMPDIR`, as Tectonic's are. With
`BIBER_CACHE` it reuses an earlier result when the arguments, every file in the folder and the
tree's version marker (`BIBER_HOME/.version`) are the same, without starting Perl. It keeps 8
results and caches only runs whose data sources are all plain files in the folder.

## Checks

`./build.sh verify` builds a minimal Android-like root with `scripts/approot.sh`: only
termux-docker's Android 9 `/system`, an app uid (10234), the libraries in a mock
`nativeLibraryDir` and the Perl tree under the app's `filesDir`, with no Termux in it. Then:

| Script | Checks |
| --- | --- |
| `scripts/compare-approot.sh` | The `.bcf` files of the seven test documents (`tests/fixtures`) give `.bbl` files byte-identical to those of the official biber 2.21 linux-aarch64 binary. |
| `scripts/tool-approot.sh` | Tectonic mode removes only the `.bib` copies, and nothing outside a Tectonic temporary folder; the cache reuses a result for the same input, runs biber again for a changed `.bib`, replaces a broken entry and clears an old unfinished one. |
| `scripts/datamodel-approot.sh` | `libbiber.so --validate-datamodel` under its own name, with `BIBER_HOME` set, reports the planted mistakes in `tests/datamodel`. |
| `scripts/suite-approot.sh` | biber's own test suite (51 files, 1264 tests) against the packaged tree. |
| `scripts/suite-forked-approot.sh` | The same suite, each test file in a child forked from a process that has already compiled the modules `bin/biber` loads, as a warm process's workers are. |
| `scripts/warm-approot.sh` | [The warm process](#the-warm-process): the seven `.bbl` files byte-identical, with the output and exit status of a run on its own; a probe in biber's place (`tests/warm/probe.pl`) reports the same state and fails the same way in 35 cases (arguments, folder, environment, umask, priority, limits, signals, buffering, a closed pipe, a full disk, a read-only folder); how the process starts and ends, and what runs do when it is absent, stalled, stale or killed. |
| `scripts/warm-peers-approot.sh` | The warm process against peers that are not runs (another user's, nonsense, too much, nothing at all), something else at its address, and a soak: 200 biber runs (24 at once) and 300 probe runs with no descriptor, zombie or memory left over. |

`./build.sh package` also runs `scripts/check-elf.py`: every library is aarch64, aligned for
16 KB pages, has no `RUNPATH`, and needs only shipped libraries and `libc`, `libm`, `libdl`.

To run a command in the root by hand: `scripts/approot.sh run 'cmd'` (as the app uid, with
`NATIVE` and `FILES` set).

## The Android library

The library is a separate Gradle build in `android/`. It takes the payload from `work/out`, or
from the folder named by the Gradle property `biber.payload` or the environment variable
`BIBER_PAYLOAD`:

```sh
cd android
./gradlew :biberdroid:testReleaseUnitTest :biberdroid:checkLegacyAbi :biberdroid:assembleRelease
```

The AAR is in `android/biberdroid/build/outputs/aar/`. Needs JDK 17 and the Android SDK
(platform 36, build tools 35). Android's `aapt2` is an x86_64 program, so on an arm64 Linux
host it has to run under qemu-user with an x86_64 sysroot (`QEMU_LD_PREFIX`); CI builds the
library on an x86_64 runner instead.

`checkLegacyAbi` compares the public API with `android/biberdroid/api/biberdroid.api` and
fails on any difference, so a change that would break apps built against an earlier release
cannot slip in. After a deliberate change, run `./gradlew :biberdroid:updateLegacyAbi` and
commit the file. Additions are compatible; removing or changing a published symbol is not,
so deprecate instead.

## The warm process

A run of biber on a small document takes 0.9 s on an Ampere Altra core; over half of it is
Perl compiling biber's modules, the same work every time. Perl cannot save compiled code to a
file, but a process that has compiled it can fork. `libbiber.so --biberdroid-warm` is such a
process: it compiles biber once, listens at the address in `BIBER_WARM`, and forks a worker
for each run that is handed to it. `Biber.warm` in the library starts one.

Measured in the Android-like root on an Ampere Altra core (`scripts/warm-bench-approot.sh`,
medians of 31 runs taken in turns), not on a phone:

| | Time | Memory |
| --- | --- | --- |
| A run on its own | 0.87 s | 94 MB at its peak, gone when it ends |
| A run a warm process did | 0.43 s | 29 MB of its own in the worker, gone when it ends |
| A warm process, from its start until it is ready | 0.49 s | 79 MB resident while it waits (68 MB of it Perl's data) |

The 0.49 s is processor time on another core than the caller's: started when a TeX engine
starts, the warm process is ready long before the engine asks for biber. Its memory is the
price, so it is meant to live only as long as runs are likely (see below).

**Whose it is.** A warm process belongs to whoever started it and lives no longer:

- It ends when its standard input reaches its end. Started by an app with a pipe as its
  input, it therefore ends with the app's process, however that dies. `/dev/null` as its
  input ends it at once.
- It ends on SIGTERM, SIGINT or SIGHUP, and after `BIBER_WARM_IDLE` seconds without a run.
- Runs under way finish first; the address is given up at once, so later runs work on
  their own.
- A run never starts a warm process and never replaces one.

**The address.** `@name` is a socket in Linux's abstract namespace, the only kind of
address: nothing in the file system, nothing to clean up, and the name is free the moment
the process gives it up (when it is told to end, also while it still compiles). The
abstract namespace has no permissions: any process on the device that knows or guesses the
name may connect to it, or listen there first. So the name should be unguessable
(`Biber.warm` makes one from 128 random bits), and the guarantee is that both sides check
that the other runs as the same user (`SO_PEERCRED`): the warm process drops any other
client unread, and a run uses nothing but a process of its own user.

**The same run.** A run that has `BIBER_WARM` in its environment asks the process there to
do it; `BIBER_TIMING` shows who did. What makes such a run the run it would have been:

- The warm process takes only runs started as it was: the same launcher file, user, Perl
  tree (its version marker) and environment. Variables that only a run's own process reads
  do not count: `BIBER_WARM`, `BIBER_WARM_WAIT`, `BIBER_WARM_IDLE`, `BIBER_CACHE`,
  `BIBER_TIMING`, and the shell's `PWD`, `OLDPWD`, `SHLVL` and `_`. So anything that was set
  up through the environment when the process started, a preloaded library that reads its
  limits from variables say, was set up as for the run. `BIBER_HOME` and `BIBER_NATIVE_DIR`
  must be absolute paths, as must every entry of `PERL5LIB`, and the tree must have its
  `.version` marker.
- The worker takes on the rest of the run: its standard input, output and error (the
  descriptors themselves, so whoever reads the run's output to its end is not kept waiting
  by the warm process, and Perl buffers them as it would in a run), working folder,
  arguments, the whole environment, umask, resource limits, priority, and blocked and
  ignored signals. Perl's own signal handlers, which biber installs while compiling
  (`sigtrap`), are active in workers only.
- The run passes the signals it receives on to the worker (HUP, INT, QUIT, TERM, PIPE,
  ALRM, USR1, USR2), and ends as the worker ended: with its exit status, or killed by the
  same signal. If the run is killed, so is its worker.
- The result cache and the Tectonic clean-up happen in the run's own process, as before.

Not carried over: the session, process group and controlling terminal (job control stops do
not reach the worker), and the parent process, which is the warm process.

**When it cannot be used.** Until a worker is ready and the run has told it to go, nothing
has been done, and the run does the work itself, as if `BIBER_WARM` were not set: when no
process listens at the address, when it is another user's, refuses (another environment, a
replaced tree or launcher), does not answer within `BIBER_WARM_WAIT` milliseconds (it may
still be compiling), or cannot set a worker up (a resource limit it may not raise). A run
with a closed standard descriptor always works on its own. After "go" the work is never done
twice: if the warm process is killed then, its worker dies with it and the run ends as
killed, with a message on standard error.

All workers of one warm process share its Perl hash seed, where every run on its own has a
new one. The seed places the keys of the hashes made while compiling, an order no run may
rely on, since it differs between two runs on their own; the order of hashes made in a run
is varied per worker.

**Limits.** 16 runs at once; more wait in the queue and work on their own after
`BIBER_WARM_WAIT`. A request (environment and arguments) may take 256 KiB. The process is
single-threaded and does not block on any peer: a request must be complete within two
seconds, a run must start the worker it was offered within two seconds, and a peer that is
another user's is dropped unread.

## Using the payload directly

Without the library, an app does what `Biber.install` does:

1. Put the files of `jniLibs/arm64-v8a` into the app's `src/main/jniLibs/arm64-v8a`, set
   `useLegacyPackaging = true` for native libraries (`android:extractNativeLibs="true"`), and
   ship `perl-tree.tar.xz` as an asset.
2. Unpack the tree into a folder of the app's own, for example `filesDir/biber`. It holds only
   folders and regular files. Write its SHA-256 (from `SHA256SUMS`) to `.version` in that
   folder; the launcher's cache uses it as the tree's version.
3. For Tectonic, make a symlink `filesDir/biber/bin/tectonic-biber` pointing to
   `nativeLibraryDir/libbiber.so`. `nativeLibraryDir` changes when the app is updated, so check
   the link on each start.
4. Run `nativeLibraryDir/libbiber.so` with `BIBER_HOME=filesDir/biber` and
   `LD_LIBRARY_PATH=nativeLibraryDir`, or put `filesDir/biber/bin` on Tectonic's `PATH`.

If the app also ships its own `libz1.so` or `libiconv.so` (a Termux build of Tectonic, say),
only one copy of each can be packaged; the README's
[section on zlib and libiconv](../README.md#if-your-app-ships-zlib-or-libiconv-too) says when
that is safe.

## Pins and the download mirror

`pins.txt` pins the termux-docker layer, biber's source, cpanm and the four Termux packages;
`scripts/cpan-pins.sha256` pins the CPAN distributions in install order; `source-pins.txt`
pins the upstream sources for the release's sources file.

Termux keeps only the newest build of each package, so the `.deb` URLs stop working after its
next update. `scripts/mirror-upload.sh` uploads every pinned file as an asset of this
repository's `pinned-downloads` release, and `fetch` falls back to it (or to `BIBER_MIRROR`).
Run it whenever a pin changes, while the new files can still be downloaded.

To move to a newer Termux package: take its `Filename` and `SHA256` from Termux's
`Packages` index, update `pins.txt` and the Termux recipe commit in `source-pins.txt`, and run
the whole build and the checks again.

## Regenerating the fixtures

The fixtures (`tests/fixtures/*/`: a `.bcf`, its `.bib` files and the reference `.bbl`) are
in the tree. `scripts/ref-run.sh` makes them again from `tests/t*.tex` with a glibc Tectonic,
a Tectonic bundle holding biblatex 3.21 and the official biber 2.21 linux-aarch64 binary; its
header has the binary's URL and checksum.

## Repository layout

| Path | Contents |
| --- | --- |
| `build.sh` | The build, step by step. |
| `pins.txt`, `scripts/cpan-pins.sha256`, `source-pins.txt` | Pinned downloads. |
| `src/` | The launcher and the module loader. |
| `scripts/` | Build, packaging, licence and check scripts. |
| `tests/` | Test documents, fixtures, the data model test, and in `tests/warm` the helpers of the warm process checks. |
| `android/` | The Android library (Gradle); `android/biberdroid/api/` holds its recorded public API. |
| `CHANGELOG.md` | What changed in each release. |
| `licenses/` | Licence texts of the components. |
| `.github/workflows/build.yml` | CI and releases. |
