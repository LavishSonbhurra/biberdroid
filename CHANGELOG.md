# Changelog

## 2.21-2

The same biber 2.21, Perl and CPAN modules, with no source of theirs changed, and the same
Termux libraries byte for byte. Two files of this project's own changed: the launcher
(`libbiber.so`, 16.8 KB instead of 15.1 KB) and `lib/BiberNative.pm` in the Perl tree. The Perl
tree's SHA-256, which is also the installed tree's version, is now
`4aab141542bb1251368c5c9cd49d8e41beb05c5d5edd6ff919ad88b0931ae33b` (2.21-1:
`191d653c...`), so an app that updates from 2.21-1 unpacks the tree once more on the first
`Biber.install`. The sizes are as before: 46 libraries of 11.8 MB and a tree of 12.5 MB in a
2.1 MB `.tar.xz`. The `.bbl` files are still byte-identical to the official biber's and
biber's 1264 tests pass. `NOTICES.txt` otherwise differs only in the address of the sources
file, which names this release.

Faster runs, with nothing to set up:

- A run on a small document takes 0.91 s instead of 1.08 s, about 16 percent less (medians
  of 15 runs each, taken in turns, in the Android-like test root on an Ampere Altra core,
  not on a phone).
- The launcher now ends a run as the `perl` program does (END blocks and destructors, then
  the process exits) instead of freeing every value first: about 0.13 s.
- `BiberNative.pm` answers the one ExtUtils::MakeMaker method that biber's start-up calls
  (`MM->maybe_command`, for its `kpsewhich` lookup), with MakeMaker's own test, so MakeMaker
  is no longer loaded on every run: about 0.05 s. MakeMaker's files are not changed.
- `BIBER_TIMING=1` prints one line on standard error saying where a run's time went (start,
  compile, run, exit); an absolute path appends the line to that file instead.

Added to the Android library, all source and binary compatible with 2.21-1:

- `Biber.installation(context, home)` describes the installation without unpacking anything:
  its paths, the installed version, and whether it is current. `applyTo` works on it, so an
  app can set up the environment for every compile without installing first. It repairs
  nothing: after an app update `isCurrent` is false until `Biber.install` relinks
  `tectonic-biber`, so call `install` whenever `isCurrent` is false (the README shows how).
- `Biber.shippedVersion(context)` returns a `BiberVersion` (the biber release and the SHA-256
  of the Perl tree in the app), and `Biber.installedVersion(context, home)` the version
  unpacked, or null. Neither installs anything.
- `Biber.processBuilder(context, workDir, args, environment)` returns the `ProcessBuilder`
  that `Biber.run` uses, for apps that start, watch and stop the process themselves.
  `Biber.run` is now built on it.
- `BiberInstallation.installedVersion`, `isInstalled` and `isCurrent`.

Changed:

- `BiberInstallation.version` is deprecated: before an install it is not clear which version
  it means. Use `installedVersion`, or `Biber.shippedVersion(context).payload`.
- What an install that was killed halfway leaves beside the tree (`biber.unpacking`,
  `biber.old`, a half-made link) is now removed by the next `Biber.install`, and by
  `Biber.installation` when no install is running, instead of only by the next unpack.
  When the kill came between the two renames that swap the trees, so that only
  `biber.old` holds a complete tree, it is moved back instead of deleted.
- `Biber.install` no longer keeps a list of homes it checked: when everything is in place it
  reads the version marker and the link, without taking the lock.
- `applyTo` no longer leaves or keeps empty entries on `PATH` or `LD_LIBRARY_PATH` (2.21-1
  wrote `dir:` when the variable held only its own folder), since an empty entry stands for
  the current folder. A `PATH` that is set but empty is treated like a missing one
  (`/system/bin`).
- biber's standard input now reads from `/dev/null` instead of a closed pipe.

Documentation and build:

- The README explains how to package the library next to an app's own `libz1.so` or
  `libiconv.so` (`pickFirsts`), and when that is safe.
- The public API is recorded in `android/biberdroid/api/biberdroid.api`, and CI checks every
  build against it.
- Every public symbol has documentation, including notes on threading.

## 2.21-1

First release.

- biber 2.21 for biblatex 3.21 (`.bcf` format 3.11), with Perl 5.44 and 112 pinned CPAN
  distributions, built inside Termux's root filesystem for arm64-v8a, Android 8.0 (API 26)
  and later, every library aligned for 16 KB pages.
- `.bbl` output byte-identical to the official biber 2.21 binary's on the test documents,
  and biber's own test suite (1264 tests) passing against the packaged tree on Android's
  bionic.
- The `tectonic-biber` mode for Tectonic: it removes the `.bib` copies Tectonic made, and
  with `BIBER_CACHE` reuses the result for unchanged input.
- The Android library `io.github.lavishsonbhurra:biberdroid:2.21-1` on Maven Central, with
  `Biber.install`, `Biber.run`, `Biber.isSupported`, `Biber.notices` and
  `BiberInstallation.applyTo`, and the payload as plain release files with notices and the
  complete sources.
