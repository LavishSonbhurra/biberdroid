#!/usr/bin/env python3
"""
Assemble the biber payload from the Termux build tree.

Run as root (the Termux rootfs is owned by uid 1000), from the host:
  sudo python3 -I scripts/package.py ROOTFS OUT

Produces
  OUT/jniLibs/arm64-v8a/   ELF pieces, APK-safe names, no RUNPATH
  OUT/assets/biber/        bin/biber + lib/ (pure Perl and data)
  OUT/manifest.tsv         every file with its source

Inputs (all inside ROOTFS):
  usr/lib/perl5/5.44.0           Termux perl core library
  home/biberlib/lib/perl5        CPAN deps + biber, installed by cpanm/Build
  home/launcher/libbiber.so      built by build-launcher.sh
"""
import os, shutil, subprocess, sys

ROOT, OUT = sys.argv[1], sys.argv[2]
T = os.path.join(ROOT, "data/data/com.termux/files")
CORE = os.path.join(T, "usr/lib/perl5/5.44.0")
CPAN = os.path.join(T, "home/biberlib/lib/perl5")
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(HERE), "src")

JNI = os.path.join(OUT, "jniLibs/arm64-v8a")
ASSETS = os.path.join(OUT, "assets/biber")
LIB = os.path.join(ASSETS, "lib")
manifest = []

# ---- pure Perl ------------------------------------------------------------

# CPAN distributions biber never loads offline: remote .bib fetching (LWP and
# TLS), XSLT output conversion, build-time tools, tests, legacy CJK codepages,
# and per-locale/timezone data biber does not use (it only knows UTC, floating
# and +hh:mm offsets, and never passes a DateTime locale).
CPAN_DENY = [
    "LWP", "LWP.pm", "HTTP", "Net", "WWW", "HTML", "Mozilla", "IO/Socket/SSL",
    "IO/Socket/SSL.pm", "XML/LibXSLT", "XML/LibXSLT.pm", "Alien", "FFI",
    "Module/Build", "Module/Build.pm", "Config/AutoConf", "Config/AutoConf.pm",
    "ExtUtils", "Test", "Test2", "Test2.pm", "Encode/HanExtra", "Encode/HanExtra.pm",
    "Encode/JIS2K", "Encode/JIS2K.pm", "Encode/EUCJPASCII", "Encode/EUCJPASCII.pm",
    "File/Listing", "File/Listing.pm", "Date", "Time/Zone.pm", "TimeDate", "POD2",
    "Text/Diff", "Text/Diff.pm", "Algorithm/Diff", "Algorithm/Diff.pm",
    "auto/share/dist/DateTime-Locale", "LWP/MediaTypes", "IO/HTML.pm",
    "perllocal.pod", ".meta", "alienfile.pm", "TimeDate.pm", "ok.pm",
    # pulled in only by the build (Alien, Module::Build) or by tests
    "Algorithm", "File/chdir.pm", "File/ShareDir/Install.pm", "MIME/Base32.pm", "Path",
    "Tie/Cycle.pm", "Encode/TW", "auto/share/dist/Alien-Libxml2", "Clone/PP.pm",
]
DT_TZ_KEEP = {"Catalog.pm", "Floating.pm", "Local.pm", "Local", "OffsetOnly.pm",
              "OlsonDB.pm", "OlsonDB", "UTC.pm"}
DT_LOCALE_KEEP = {"Base.pm", "Catalog.pm", "Conflicts.pm", "Data.pm", "FromData.pm", "Util.pm"}
SKIP_EXT = (".pod", ".packlist", ".h", ".exists", ".bs")


def denied(rel):
    for d in CPAN_DENY:
        if rel == d or rel.startswith(d + "/"):
            return True
    parts = rel.split("/")
    if parts[:2] == ["DateTime", "TimeZone"] and len(parts) > 2 and parts[2] not in DT_TZ_KEEP:
        return True
    if parts[:2] == ["DateTime", "Locale"] and len(parts) > 2 and parts[2] not in DT_LOCALE_KEEP:
        return True
    return False


def copy(src, rel, origin):
    if rel.endswith(SKIP_EXT) or rel.endswith(".so"):
        return
    dst = os.path.join(LIB, rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)
    manifest.append(("asset", "lib/" + rel, origin))


def walk(base, origin, filt):
    for dirpath, dirnames, files in os.walk(base):
        dirnames.sort()
        for f in sorted(files):
            src = os.path.join(dirpath, f)
            rel = os.path.relpath(src, base)
            # The arch folder merges into the same tree: @INC needs one entry.
            if rel.startswith("aarch64-android/"):
                rel = rel[len("aarch64-android/"):]
            if rel.startswith("auto/") and not rel.startswith("auto/share/") and not rel.endswith((".ix", ".al")):
                continue
            if filt(rel):
                copy(src, rel, origin)


walk(CPAN, "cpan", lambda rel: not denied(rel))

# Core: what biber's test suite and runs actually opened (strace), minus the
# test harness, plus the data perl reads lazily by name: every collation
# locale, the CJK collation helpers, charnames tables and Encode's .pm files.
CORE_DENY = ("App/", "TAP/", "Test/", "Test2", "Benchmark.pm",
             "CPAN", "Pod/Perldoc", "perl5db.pl")
core_files = set()
# core-files.txt: core library files opened (strace) by biber's test suite and
# the test documents on the Termux build, relative to the core lib folder.
for line in open(os.path.join(HERE, "core-files.txt")):
    rel = line.strip()
    if rel and os.path.isfile(os.path.join(CORE, rel)) and not rel.startswith(CORE_DENY):
        core_files.add(rel)
for sub in ("Unicode/Collate", "aarch64-android/Unicode/Collate", "aarch64-android/Encode", "Encode"):
    for dirpath, _, files in os.walk(os.path.join(CORE, sub)):
        for f in files:
            rel = os.path.relpath(os.path.join(dirpath, f), CORE)
            if f.endswith((".pm", ".pl")):
                core_files.add(rel)
for extra in ("aarch64-android/DynaLoader.pm", "unicore/Name.pl", "unicore/Name.pm", "unicore/UCD.pl", "unicore/version",
              "aarch64-android/attributes.pm", "aarch64-android/Hash/Util.pm",
              "aarch64-android/MIME/Base64.pm", "aarch64-android/MIME/QuotedPrint.pm"):
    core_files.add(extra)
for rel in sorted(core_files):
    out = rel[len("aarch64-android/"):] if rel.startswith("aarch64-android/") else rel
    if out.startswith("Unicode/Collate/") and out.endswith((".txt",)):
        continue
    copy(os.path.join(CORE, rel), out, "perl-core")

shutil.copyfile(os.path.join(SRC, "BiberNative.pm"), os.path.join(LIB, "BiberNative.pm"))
manifest.append(("asset", "lib/BiberNative.pm", "ours"))
os.makedirs(os.path.join(ASSETS, "bin"), exist_ok=True)
shutil.copyfile(os.path.join(T, "home/biberlib/bin/biber"), os.path.join(ASSETS, "bin/biber"))
manifest.append(("asset", "bin/biber", "biber"))

# ---- ELF ------------------------------------------------------------------

os.makedirs(JNI, exist_ok=True)
RENAME_NEEDED = {"libxml2.so.16": "libxml216.so", "libz.so.1": "libz1.so"}


def patch(path, soname=None):
    subprocess.check_call(["patchelf", "--remove-rpath", path])
    out = subprocess.check_output(["patchelf", "--print-needed", path], text=True).split()
    for old, new in RENAME_NEEDED.items():
        if old in out:
            subprocess.check_call(["patchelf", "--replace-needed", old, new, path])
    # Termux links everything to libandroid-utimes for futimes/lutimes, which
    # bionic has had since API 26 (the app's minSdk). It is also the one piece
    # aligned for 4 KB pages only, so it would not load on a 16 KB device.
    if "libandroid-utimes.so" in out:
        subprocess.check_call(["patchelf", "--remove-needed", "libandroid-utimes.so", path])
    if soname:
        subprocess.check_call(["patchelf", "--set-soname", soname, path])


def ship(src, name, origin, soname=None):
    dst = os.path.join(JNI, name)
    shutil.copyfile(src, dst)
    os.chmod(dst, 0o755)
    patch(dst, soname)
    manifest.append(("jni", name, origin))


ship(os.path.join(T, "home/launcher/libbiber.so"), "libbiber.so", "ours")
ship(os.path.join(CORE, "aarch64-android/CORE/libperl.so"), "libperl.so", "termux:perl")
ship(os.path.join(T, "usr/lib/libxml2.so.16"), "libxml216.so", "termux:libxml2", soname="libxml216.so")
# libxml2's own deps. Versioned sonames become APK-safe names (libz.so.1 ->
# libz1.so, which also keeps clear of the system's private libz.so).
ship(os.path.join(T, "usr/lib/libz.so.1"), "libz1.so", "termux:zlib", soname="libz1.so")
ship(os.path.join(T, "usr/lib/libiconv.so"), "libiconv.so", "termux:libiconv")

# XS modules: everything biber's tests and runs loaded, plus a few small
# core ones loaded by name at run time.
# xs-files.txt: XS objects loaded (DynaLoader) by the suite and test runs,
# plus a few small core ones loaded by name at run time, as auto/ paths.
so_list = []
for line in open(os.path.join(HERE, "xs-files.txt")):
    rel = line.strip()
    if not rel:
        continue
    cpan = os.path.join(CPAN, "aarch64-android/auto", rel)
    so_list.append(cpan if os.path.exists(cpan) else os.path.join(CORE, "aarch64-android/auto", rel))
for src in sorted(set(so_list)):
    rel = src.split("/auto/", 1)[1]              # Foo/Bar/Bar.so
    module = "_".join(rel.split("/")[:-1])       # Foo_Bar
    origin = "perl-core" if src.startswith(CORE) else "cpan"
    ship(src, "libpl_%s.so" % module, origin)

with open(os.path.join(OUT, "manifest.tsv"), "w") as f:
    for kind, name, origin in manifest:
        f.write("%s\t%s\t%s\n" % (kind, name, origin))
print("jni:", sum(1 for m in manifest if m[0] == "jni"), "assets:", sum(1 for m in manifest if m[0] == "asset"))
