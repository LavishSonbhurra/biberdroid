#!/usr/bin/env python3
"""
One plain-text notice for everything the payload ships, for an app's
licences screen:

  python3 -I scripts/notices.py OUT [CPAN_TARBALL_DIR]

Reads OUT/COMPONENTS.tsv (licenses.py) and the licence texts in licenses/,
and writes OUT/NOTICES.txt: what the payload is and where its source is, one
line per component, then each licence text once. Where a component offers a
choice of licences, the notice names the one it is distributed under here.
MIT-style dists carry their own copyright line, so their LICENSE file is
copied from the pinned tarball. Fails on a licence it has no text or rule for.
"""
import os, re, sys, tarfile

OUT = sys.argv[1]
HERE = os.path.dirname(os.path.abspath(__file__))
B = os.path.dirname(HERE)
TARBALLS = sys.argv[2] if len(sys.argv) > 2 else os.path.join(B, "work/dl/pinned/cpan")
LIC = os.path.join(B, "licenses")

# Licence as licenses.py writes it -> (what this distribution uses, text file or None).
CHOSEN = {
    "Artistic-1.0-Perl OR GPL-1.0-or-later": ("the Artistic License 1.0 (Perl's terms)", "Artistic-1.0-Perl.txt"),
    "artistic_1 / gpl_2": ("the Artistic License 1.0", "Artistic-1.0-Perl.txt"),
    "BSD/Artistic/GPL (IPC-Run3: any of)": ("the Artistic License 1.0", "Artistic-1.0-Perl.txt"),
    "MIT / bsd / artistic_1 / Artistic-2.0": ("the Artistic License 2.0", "Artistic-2.0.txt"),
    "Artistic-2.0": ("the Artistic License 2.0", "Artistic-2.0.txt"),
    "Apache-2.0": ("the Apache License 2.0", "Apache-2.0.txt"),
    "LGPL-2.1-or-later (library)": ("the GNU LGPL 2.1 or later", "LGPL-2.1.txt"),
    "Zlib": ("the zlib licence", "zlib.txt"),
    "Unrestricted (XML-Writer)": ("no restrictions (its author's grant)", None),
}
PERL_UNICODE = ("Artistic-1.0-Perl OR GPL-1.0-or-later; lib/unicore and the collation table in "
                "libpl_Unicode_Collate.so are Unicode data (Unicode-3.0)")
TITLES = {
    "Artistic-1.0-Perl.txt": "The Artistic License 1.0 (as distributed with Perl)",
    "Artistic-2.0.txt": "The Artistic License 2.0",
    "Apache-2.0.txt": "Apache License 2.0",
    "LGPL-2.1.txt": "GNU Lesser General Public License 2.1",
    "zlib.txt": "zlib licence",
    "libxml2.txt": "libxml2 licence (MIT)",
    "Unicode-3.0.txt": "Unicode License v3",
}


def mit_license(name, version):
    """The LICENSE (or COPYRIGHT) file of an MIT-licensed CPAN dist."""
    tb = os.path.join(TARBALLS, f"{name}-{version}.tar.gz")
    with tarfile.open(tb) as t:
        for m in t.getmembers():
            if m.isfile() and m.name.count("/") == 1 and re.search(r"/(LICEN[CS]E|COPYING|COPYRIGHT)$", m.name):
                return t.extractfile(m).read().decode("utf-8", "replace").strip()
    sys.exit(f"no licence file in {tb}")


rows = []
with open(os.path.join(OUT, "COMPONENTS.tsv")) as f:
    next(f)
    for line in f:
        name, version, licence, source, _ = line.rstrip("\n").split("\t")
        rows.append((name, version, licence, source))

used, own = [], []
lines = []
for name, version, licence, source in rows:
    if licence == PERL_UNICODE:
        terms = "the Artistic License 1.0 (Perl's terms); its Unicode data under the Unicode License v3"
        texts = ["Artistic-1.0-Perl.txt", "Unicode-3.0.txt"]
    elif name == "libxml2":
        terms, texts = "the MIT licence below", ["libxml2.txt"]
    elif licence == "MIT":
        terms, texts = "the MIT licence (its notice below)", []
        own.append((name, mit_license(name, version)))
    elif licence in CHOSEN:
        terms, text = CHOSEN[licence]
        texts = [text] if text else []
    else:
        sys.exit(f"no rule for licence {licence!r} ({name})")
    for t in texts:
        if t not in used:
            used.append(t)
    if name == "biber-android":
        lines.append(f"- biber-android, the launcher and module loader written for this build: {terms}. Source: {source}")
    else:
        lines.append(f"- {name} {version}: {terms}. Source: {source}")

out = []
out.append("biber 2.21 for Android, https://github.com/plk/biber\n\n"
           "biber processes biblatex bibliographies. This build runs biber's own Perl code\n"
           "on Perl 5.44 with the CPAN modules it needs, using Termux's builds of Perl,\n"
           "libxml2, zlib and libiconv (https://github.com/termux/termux-packages). None\n"
           "of their source was changed; the libraries were only renamed and relinked with\n"
           "patchelf. Each release of https://github.com/LavishSonbhurra/biber-android\n"
           "also carries the complete source of this build as one file\n"
           "(biber-android-<version>-sources.tar). The components, their licences and\n"
           "where to get their source:\n")
out.append("\n".join(lines) + "\n")
out.append("The GNU LGPL lets you replace libiconv with your own build: the library is\n"
           "libiconv.so in the app's native library folder, and its source is at the\n"
           "address above and in the sources file of the release.\n")
for name, text in own:
    out.append(f"{name}\n\n{text}\n")
for t in used:
    with open(os.path.join(LIC, t), encoding="utf-8") as f:
        out.append(f"{TITLES[t]}\n\n{f.read().strip()}\n")
text = ("\n" + "-" * 72 + "\n\n").join(out)
with open(os.path.join(OUT, "NOTICES.txt"), "w", encoding="utf-8") as f:
    f.write(text)
print(f"NOTICES.txt: {len(rows)} components, {len(used) + len(own)} licence texts, {len(text)} characters")
