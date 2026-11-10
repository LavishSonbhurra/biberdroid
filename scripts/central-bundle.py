#!/usr/bin/env python3
"""Packs a signed Maven repository directory into a Central Portal bundle.

Takes the directory that `./gradlew :biber:publishReleasePublicationToStagingRepository`
wrote with a signing key set (android/build/central-staging) and zips it with the same
layout. Gradle's maven-metadata files are left out, since Central makes its own. Every
artifact must have its .asc signature and correct .md5 and .sha1 files, or nothing is
written.

    central-bundle.py --repo android/build/central-staging --out bundle.zip
"""
import argparse
import hashlib
import pathlib
import sys
import zipfile

SIDECARS = (".md5", ".sha1", ".sha256", ".sha512", ".asc")
ZIP_TIME = (1980, 2, 1, 0, 0, 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--repo", type=pathlib.Path, required=True)
    ap.add_argument("--out", type=pathlib.Path, required=True)
    args = ap.parse_args()

    files = sorted(p for p in args.repo.rglob("*") if p.is_file() and not p.name.startswith("maven-metadata"))
    artifacts = [p for p in files if not p.name.endswith(SIDECARS)]
    if not any(p.suffix == ".pom" for p in artifacts):
        sys.exit(f"{args.repo}: no POM found")
    problems = []
    for p in artifacts:
        data = p.read_bytes()
        for suffix, digest in ((".md5", hashlib.md5), (".sha1", hashlib.sha1)):
            side = p.with_name(p.name + suffix)
            if not side.is_file():
                problems.append(f"{side}: missing")
            elif side.read_text().strip() != digest(data).hexdigest():
                problems.append(f"{side}: wrong checksum")
        if not p.with_name(p.name + ".asc").is_file():
            problems.append(f"{p}.asc: missing (was a signing key set?)")
    if problems:
        sys.exit("\n".join(problems))

    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as bundle:
        for path in files:
            info = zipfile.ZipInfo(path.relative_to(args.repo).as_posix(), ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            bundle.writestr(info, path.read_bytes())
    print(f"{args.out}: {len(artifacts)} artifacts, {len(files)} files, {args.out.stat().st_size} bytes")


if __name__ == "__main__":
    main()
