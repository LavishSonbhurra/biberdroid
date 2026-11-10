#!/usr/bin/env python3
"""
Packs the Perl tree as a reproducible ustar inside xz:

  python3 -I scripts/archive.py TREE OUT.tar.xz

Fixed entry order, times, owners and modes, so the same tree always gives the
same bytes and the archive's SHA-256 can serve as the tree's version. Only
folders and regular files go in, which is all the Android library's reader
accepts. The xz dictionary is 16 MiB: it holds the whole tree, and the default
for -9 (64 MiB) would make a phone's decoder allocate that much for nothing.
"""
import lzma
import os
import sys
import tarfile


def reset(info):
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    info.mtime = 0
    info.mode = 0o755 if info.isdir() else 0o644
    return info


def main():
    src, dst = sys.argv[1], sys.argv[2]
    paths = []
    for dirpath, dirnames, files in os.walk(src):
        dirnames.sort()
        rel = os.path.relpath(dirpath, src)
        if rel != ".":
            paths.append(rel)
        paths += [os.path.normpath(os.path.join(rel, f)) for f in sorted(files)]
    for p in paths:
        full = os.path.join(src, p)
        if os.path.islink(full) or not (os.path.isdir(full) or os.path.isfile(full)):
            sys.exit(f"{p}: only folders and regular files can go in")
    filters = [{"id": lzma.FILTER_LZMA2, "preset": 9 | lzma.PRESET_EXTREME, "dict_size": 16 << 20}]
    with lzma.open(dst, "wb", format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC64, filters=filters) as xz:
        with tarfile.open(fileobj=xz, mode="w", format=tarfile.USTAR_FORMAT) as tar:
            for p in paths:
                tar.add(os.path.join(src, p), arcname=p, recursive=False, filter=reset)
    print(f"{dst}: {len(paths)} entries, {os.path.getsize(dst)} bytes")


if __name__ == "__main__":
    main()
