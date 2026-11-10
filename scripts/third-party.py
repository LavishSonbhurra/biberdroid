#!/usr/bin/env python3
"""
Writes THIRD_PARTY.md from the build's COMPONENTS.tsv (scripts/licenses.py):

  python3 -I scripts/third-party.py work/out/COMPONENTS.tsv > THIRD_PARTY.md

CI checks that the committed file matches the build.
"""
import re
import sys

HEAD = """# Third-party components

The payload (the files in `jniLibs/arm64-v8a` and the Perl tree) is made of the components
below, generated from the build's `COMPONENTS.tsv`. Each release also carries:

- `NOTICES.txt`: this list with the full licence texts, for an app's licences screen.
  The AAR holds the same file as `assets/biber-android/NOTICES.txt`.
- `biber-android-<version>-sources.tar`: the complete source of everything listed here.
- `COMPONENTS.tsv` and, inside the payload archive, `FILES.tsv`, which maps every shipped
  file to its component.

Where a component lets the recipient choose a licence (Perl and most CPAN modules: the
Artistic License or the GPL), it is distributed here under the Artistic License. The licence
texts are in [licenses/](licenses).

| Component | Version | Licence | Source | Files |
| --- | --- | --- | --- | ---: |
"""


def cell(text):
    return text.replace("|", "\\|")


def links(source):
    parts = []
    for part in source.split(" + "):
        m = re.match(r"(https?://\S+)(.*)", part)
        parts.append(f"<{m.group(1)}>{m.group(2)}" if m else part)
    return "<br>".join(parts)


def main():
    rows = []
    with open(sys.argv[1], encoding="utf-8") as f:
        next(f)
        for line in f:
            name, version, licence, source, files = line.rstrip("\n").split("\t")
            rows.append(f"| {cell(name)} | {cell(version)} | {cell(licence)} | {cell(links(source))} | {files} |")
    sys.stdout.write(HEAD + "\n".join(rows) + "\n")


if __name__ == "__main__":
    main()
