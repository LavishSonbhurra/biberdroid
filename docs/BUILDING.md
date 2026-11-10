# Building biber-android

- [Host](#host)
- [Steps](#steps)
- [What is shipped and how it is made](#what-is-shipped-and-how-it-is-made)
- [Checks](#checks)
- [The Android library](#the-android-library)
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

No source of biber, Perl or any CPAN module is changed. The ELF files from Termux are changed
only with patchelf:

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
It also drops Termux's prefix from `@INC`.

The launcher's environment:

| Variable | Meaning | Default |
| --- | --- | --- |
| `BIBER_HOME` | The Perl tree. | The folder above the one holding `argv[0]`, so a link at `BIBER_HOME/bin/tectonic-biber` needs no variable. |
| `BIBER_NATIVE_DIR` | Where the `libpl_*.so` files are. | The folder of `/proc/self/exe`, which is `nativeLibraryDir`. |
| `BIBER_CACHE` | Optional folder for reusing results in Tectonic mode. | None. |
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
./gradlew :biber:testReleaseUnitTest :biber:assembleRelease
```

The AAR is in `android/biber/build/outputs/aar/`. Needs JDK 17 and the Android SDK
(platform 36, build tools 35). Android's `aapt2` is an x86_64 program, so on an arm64 Linux
host it has to run under qemu-user with an x86_64 sysroot (`QEMU_LD_PREFIX`); CI builds the
library on an x86_64 runner instead.

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
they must be the same Termux builds as the ones pinned here.

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
| `tests/` | Test documents, fixtures and the data model test. |
| `android/` | The Android library (Gradle). |
| `licenses/` | Licence texts of the components. |
| `.github/workflows/build.yml` | CI and releases. |
