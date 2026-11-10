# biber-android

[![build](https://github.com/LavishSonbhurra/biber-android/actions/workflows/build.yml/badge.svg)](https://github.com/LavishSonbhurra/biber-android/actions/workflows/build.yml)
[![release](https://img.shields.io/github/v/release/LavishSonbhurra/biber-android)](https://github.com/LavishSonbhurra/biber-android/releases/latest)
[![license](https://img.shields.io/github/license/LavishSonbhurra/biber-android)](LICENSE)

[biber](https://github.com/plk/biber), the bibliography processor behind biblatex, built to run
inside an Android app, with no Termux installed on the device.

It was made for an Android LaTeX editor that compiles with Tectonic, and works with any app
that needs to turn a biblatex `.bcf` file into a `.bbl`.

## What you get

- **biber 2.21**, unchanged, with Perl 5.44 and the 112 CPAN distributions it uses
- **arm64-v8a**, **Android 8.0 (API 26) and later**; every library is aligned for 16 KB pages
- **Size**: 46 libraries, 11.8 MB (3.9 MB compressed), and a Perl tree of 12.5 MB that ships
  as a 2.1 MB `.tar.xz`
- **Output** byte-identical to the official biber 2.21 binary's on the test documents
- An **Android library** (AAR) that installs and runs it, or plain release files if you
  package it yourself

biber and biblatex must be used in matching versions. biber refuses a `.bcf` file whose format
it does not know.

| biber-android | biber | biblatex | `.bcf` format |
| --- | --- | --- | --- |
| 2.21-x | 2.21 | 3.21 | 3.11 |

## Quick start

### Gradle

```kotlin
// build.gradle.kts of the app
android {
    defaultConfig {
        minSdk = 26
    }
    packaging {
        jniLibs {
            // biber is started as a program from nativeLibraryDir, so the libraries
            // must be extracted when the app is installed.
            useLegacyPackaging = true
            // They are already stripped and were edited with patchelf: leave them as they are.
            keepDebugSymbols += listOf(
                "**/libbiber.so", "**/libperl.so", "**/libxml216.so",
                "**/libz1.so", "**/libiconv.so", "**/libpl_*.so",
            )
        }
    }
}

dependencies {
    implementation("io.github.lavishsonbhurra:biber-android:2.21-1")
}
```

Then, off the main thread:

```kotlin
import io.github.lavishsonbhurra.biber.Biber

// workDir holds main.bcf (written by a LaTeX run) and the .bib files it names.
val result = Biber.run(context, workDir, listOf("main"))
if (!result.succeeded) Log.w("biber", result.output)
```

`Biber.run` unpacks the Perl tree into `filesDir/biber` on first use (a second or two), starts
biber, waits for it and returns its exit code and output. `Biber.isSupported(context)` tells
whether biber can run on the device, and `Biber.notices(context)` returns the licence notices
for your app's licences screen.

### With Tectonic

Tectonic runs `tectonic-biber` from `PATH` whenever a document uses biblatex.
`Biber.install` creates that name as a link to the launcher, and `applyTo` sets up the
environment:

```kotlin
val biber = Biber.install(context)
val tex = ProcessBuilder(tectonicPath, "main.tex").directory(projectDir)
// Puts biber's bin folder on PATH and nativeLibraryDir on LD_LIBRARY_PATH.
biber.applyTo(tex.environment())
```

Started as `tectonic-biber`, the launcher also removes the `.bib` copies Tectonic made for it,
which Tectonic would otherwise write back into the document's folder. With a cache folder,
`biber.applyTo(env, cacheDir)`, it reuses the previous result when the input has not changed.

### Without Gradle: the release files

Each [release](https://github.com/LavishSonbhurra/biber-android/releases) has the payload as
plain files:

| File | Contents |
| --- | --- |
| `biber-android-<version>-jniLibs-arm64-v8a.tar.gz` | `jniLibs/arm64-v8a/*.so` |
| `biber-android-<version>-perl-tree.tar.xz` | the Perl tree (`bin/biber`, `lib/`) |
| `biber-android-<version>-android-arm64.tar.gz` | both, unpacked, with file lists and notices |
| `biber-android-<version>.aar` | the Android library |
| `NOTICES.txt`, `COMPONENTS.tsv` | licences and sources of every component |
| `biber-android-<version>-sources.tar` | the complete source of the payload |
| `SHA256SUMS` | checksums of all of the above |

```sh
tar -xzf biber-android-2.21-1-jniLibs-arm64-v8a.tar.gz -C app/src/main
```

Unpack the Perl tree to a folder of the app's own at run time, then start
`nativeLibraryDir/libbiber.so` with `BIBER_HOME` set to that folder and `nativeLibraryDir` on
`LD_LIBRARY_PATH`. [docs/BUILDING.md](docs/BUILDING.md#using-the-payload-directly) has the
details.

## How it works

- `libbiber.so` is a small launcher ([src/biber-launcher.c](src/biber-launcher.c)) that embeds
  Perl and runs biber's own script from the unpacked tree. Android only lets an app targeting
  API 29 or later start programs from its `nativeLibraryDir`, and only installs files named
  `lib*.so` there, hence the name.
- `libperl.so`, `libxml216.so`, `libz1.so` and `libiconv.so` are Termux's builds, with
  versioned names made APK-safe and their `RUNPATH` removed by patchelf.
- The compiled parts of Perl modules ship as `libpl_<Module>.so`.
  [src/BiberNative.pm](src/BiberNative.pm) makes Perl load them from `nativeLibraryDir` instead
  of its usual `auto/` folders.
- The rest, biber and the pure-Perl modules, is the Perl tree.

The build runs inside Termux's root filesystem on an arm64 Linux host with `sudo chroot`.
The Linux kernel runs Android's bionic binaries directly, so Termux's own Perl and clang do
the work, with no emulator. Every download is pinned by SHA-256.

## Building from source

On an arm64 Linux host with sudo, curl, python3, patchelf and git (a GitHub
`ubuntu-24.04-arm` runner works) and about 2 GB of disk; it takes under 10 minutes on a
2-core VM once the downloads are in place:

```sh
./build.sh                 # payload in work/out, release files in work/dist
cd android
./gradlew :biber:assembleRelease
```

`./build.sh` also checks the result in a minimal Android-like root: the `.bbl` files must be
byte-identical to those of the official biber 2.21 binary, and biber's own test suite (1264
tests) must pass against the packaged tree. See [docs/BUILDING.md](docs/BUILDING.md) for the
steps, the checks and how to update a pin, and [docs/RELEASING.md](docs/RELEASING.md) for
releases.

## Limitations

- arm64-v8a only. On other devices `Biber.isSupported` is false.
- No remote data sources: the network modules (LWP, TLS) are left out, so a `.bib` file must be
  a local file.
- The extra CJK encodings of Encode::HanExtra, Encode::JIS2K and Encode::EUCJPASCII are left
  out. UTF-8 and the encodings in Perl's core Encode work.
- XML::LibXSLT is left out, so `--convert-control` (the `.bcf` to HTML conversion) does not
  work.
- DateTime's per-locale data and most time zones are left out. biber only uses UTC, floating
  times and fixed offsets, and never a DateTime locale.
- Each run starts Perl, which takes about a second (measured on an Ampere Altra core). The
  `tectonic-biber` cache skips runs whose input has not changed.

## Status

The payload is built and checked on Android 9's bionic in a chroot (the checks above), and its
files are identical to a build that ships in an app. Not yet verified: the GitHub Actions
workflow, which has not run, and this repository's AAR inside an app on a device.

## Licences

The code in this repository is under the [Artistic License 2.0](LICENSE), the licence of
biber itself. The payload is made of biber, Perl, CPAN modules, libxml2, zlib and libiconv,
each under its own licence: [THIRD_PARTY.md](THIRD_PARTY.md) lists them all, and
[licenses/](licenses) holds the texts. An app that ships the payload should show
`NOTICES.txt` (`Biber.notices(context)`). libiconv is under the GNU LGPL: it stays a separate
`libiconv.so` that users can replace, and each release carries its source. The library also
depends on [XZ for Java](https://tukaani.org/xz/java.html) (0BSD).

## Credits

- [biber](https://github.com/plk/biber) by François Charette and Philip Kime, and
  [biblatex](https://github.com/plk/biblatex).
- [Perl](https://www.perl.org) and the authors of the CPAN modules biber uses.
- [Termux](https://termux.dev): its packages of Perl, libxml2, zlib and libiconv, and
  [termux-docker](https://github.com/termux/termux-docker), whose root filesystem hosts the
  build.
- The `biber-installer` package of the
  [Termux User Repository](https://github.com/termux-user-repository/tur), which builds biber
  on a device with cpanm, was the prior art for this build.
