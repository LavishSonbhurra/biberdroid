#!/usr/bin/env python3
"""
Notices for redistribution: maps every shipped file to the upstream component
it came from, with version, licence and source URL.

  sudo python3 -I scripts/licenses.py ROOTFS OUT [CPAN_TARBALL_DIR]

Reads OUT/manifest.tsv (written by package.py), the .packlist files the
installs left in the Termux rootfs, and the pinned CPAN tarballs (default
dl/pinned/cpan) for each dist's version and licence. Writes
  OUT/COMPONENTS.tsv   component, version, licence, source, shipped files
  OUT/FILES.tsv        kind, shipped name, component
Fails if any shipped file cannot be attributed.
"""
import collections, glob, json, os, sys

ROOT, OUT = sys.argv[1], sys.argv[2]
T = os.path.join(ROOT, "data/data/com.termux/files")
TPREFIX = "/data/data/com.termux/files"
CPAN = os.path.join(T, "home/biberlib/lib/perl5")
TERMUX_PKGS = "https://github.com/termux/termux-packages/tree/master/packages/"

# Licences cpanm could not read from the metadata, taken from each dist's POD.
LICENSE_FIX = {"Data-Uniqid": "perl_5", "Sort-Key": "perl_5", "IO-String": "perl_5",
               "File-Find-Rule": "perl_5", "Number-Compare": "perl_5", "XML-SAX": "perl_5",
               "Algorithm-Diff": "perl_5", "Encode-JIS2K": "perl_5"}
SPDX = {"perl_5": "Artistic-1.0-Perl OR GPL-1.0-or-later", "perl": "Artistic-1.0-Perl OR GPL-1.0-or-later", "artistic_2": "Artistic-2.0",
        "apache_2_0": "Apache-2.0", "mit": "MIT", "unrestricted": "Unrestricted (XML-Writer)",
        "open_source": "BSD/Artistic/GPL (IPC-Run3: any of)"}

FIXED = {
    "perl": ("5.44.0 (Termux perl 5.44.0-1)",
             "Artistic-1.0-Perl OR GPL-1.0-or-later; lib/unicore and the collation table in "
             "libpl_Unicode_Collate.so are Unicode data (Unicode-3.0)",
             "https://www.cpan.org/src/5.0/perl-5.44.0.tar.gz + " + TERMUX_PKGS + "perl"),
    "biber": ("2.21", "Artistic-2.0", "https://github.com/plk/biber/archive/refs/tags/v2.21.tar.gz"),
    "libxml2": ("2.15.4 (Termux libxml2 2.15.4-1)", "MIT",
                "https://download.gnome.org/sources/libxml2/2.15/libxml2-2.15.4.tar.xz + " + TERMUX_PKGS + "libxml2"),
    "zlib": ("1.4 (Termux zlib 1.4)", "Zlib", "https://zlib.net/ + " + TERMUX_PKGS + "zlib"),
    "libiconv": ("1.19.1 (Termux libiconv 1.19.1)", "LGPL-2.1-or-later (library)",
                 "https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.19.1.tar.gz + " + TERMUX_PKGS + "libiconv"),
    "biber-android": ("-", "Artistic-2.0", "https://github.com/LavishSonbhurra/biber-android (src/biber-launcher.c, src/BiberNative.pm)"),
}

# dist -> (version, licence, url), read from each pinned tarball's META file.
# installed path -> dist: each .packlist (one per installed dist) goes to the
# tarball whose module files best match the paths it lists.
import re, tarfile
HERE = os.path.dirname(os.path.abspath(__file__))
TARBALLS = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(HERE), "work/dl/pinned/cpan")
owner, dists, files_of = {}, {}, {}
for line in open(os.path.join(HERE, "cpan-pins.sha256")):
    if line.startswith("#") or not line.strip():
        continue
    url = line.split()[1]
    tb = os.path.basename(url)
    dist = re.sub(r"\.(tar\.gz|tgz)$", "", tb)
    with tarfile.open(os.path.join(TARBALLS, tb)) as t:
        names = [m.name.split("/", 1)[1] for m in t.getmembers() if "/" in m.name and m.isfile()]
        meta, lic, ver = None, "unknown", dist.rsplit("-", 1)[1]
        for cand in ("META.json", "META.yml"):
            if cand in names:
                meta = t.extractfile([m for m in t.getmembers() if m.name.endswith("/" + cand) and m.name.count("/") == 1][0]).read().decode("utf-8", "replace")
                break
    if meta and meta.lstrip().startswith("{"):
        j = json.loads(meta)
        lic = ",".join(j.get("license", ["unknown"])) if isinstance(j.get("license"), list) else str(j.get("license", "unknown"))
        ver = str(j.get("version", ver))
    elif meta:
        m = re.search(r"^license:\s*'?([\w.]+)", meta, re.M)
        lic = m.group(1) if m else "unknown"
        m = re.search(r"^version:\s*'?([\w.]+)", meta, re.M)
        ver = m.group(1) if m else ver
    base = dist.rsplit("-", 1)[0]
    if "unknown" in lic:
        lic = LICENSE_FIX.get(base, lic)
    lic = " / ".join(SPDX.get(x, x) for x in dict.fromkeys(lic.split(",")) if x != "unknown") or "unknown"
    dists[dist] = (ver, lic, url)
    files_of[dist] = [n[4:] if n.startswith("lib/") else n for n in names if n.endswith((".pm", ".xs"))]

# A .packlist sits at auto/<main module>/.packlist. Its dist is the one named
# after that module, else the tarball that holds the module's own .pm/.xs.
all_names = {}
for dist in files_of:
    all_names[dist] = set(files_of[dist])
for packlist in glob.glob(os.path.join(CPAN, "aarch64-android/auto/**/.packlist"), recursive=True):
    mod = os.path.relpath(os.path.dirname(packlist), os.path.join(CPAN, "aarch64-android/auto"))
    want = mod.replace("/", "-")
    best = next((d for d in dists if d.rsplit("-", 1)[0] == want), None)
    if best is None:
        leaf = mod.split("/")[-1]
        best = next((d for d, fs in all_names.items()
                     if mod + ".pm" in fs or leaf + ".pm" in fs or leaf + ".xs" in fs), None)
    if best is None:
        continue
    for l in open(packlist):
        if l.strip():
            owner[l.split()[0]] = best

rows, missing = [], []
for line in open(os.path.join(OUT, "manifest.tsv")):
    kind, name, origin = line.rstrip("\n").split("\t")
    comp = None
    if origin == "perl-core" or origin == "termux:perl":
        comp = "perl"
    elif origin == "biber" or name.startswith(("lib/Biber/", "lib/Biber.pm")):
        comp = "biber"
    elif origin.startswith("termux:"):
        comp = origin.split(":", 1)[1]
    elif origin == "ours":
        comp = "biber-android"
    elif origin == "cpan":
        if kind == "asset":
            rel = name[len("lib/"):]
            cands = [f"{TPREFIX}/home/biberlib/lib/perl5/{rel}",
                     f"{TPREFIX}/home/biberlib/lib/perl5/aarch64-android/{rel}"]
        else:  # libpl_Foo_Bar.so: find the packlisted .so it was made from
            cands = [p for p in owner if p.endswith(".so") and "/auto/" in p and
                     "libpl_" + "_".join(p.split("/auto/", 1)[1].split("/")[:-1]) + ".so" == name]
        for c in cands:
            if c in owner:
                comp = owner[c]
                break
    if comp is None and name == "lib/XML/SAX/ParserDetails.ini":
        comp = next(d for d in dists if d.startswith("XML-SAX-1"))  # written by XML::SAX at install
    if comp is None:
        missing.append((kind, name, origin))
    rows.append((kind, name, comp))

if missing:
    sys.exit("unattributed files:\n" + "\n".join("\t".join(m) for m in missing))

count = collections.Counter(c for _, _, c in rows)
with open(os.path.join(OUT, "COMPONENTS.tsv"), "w") as f:
    f.write("component\tversion\tlicence\tsource\tshipped files\n")
    for comp in sorted(count, key=lambda c: (c not in FIXED, c.lower())):
        ver, lic, url = FIXED.get(comp) or dists[comp]
        name = comp.rsplit("-", 1)[0] if comp in dists else comp
        f.write(f"{name}\t{ver}\t{lic}\t{url}\t{count[comp]}\n")
with open(os.path.join(OUT, "FILES.tsv"), "w") as f:
    for r in rows:
        f.write("\t".join(r) + "\n")
print(f"{len(count)} components, {len(rows)} files attributed")
