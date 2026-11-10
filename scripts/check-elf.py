#!/usr/bin/env python3
"""
Checks the ELF files of a jniLibs folder before they ship:

  python3 -I scripts/check-elf.py DIR

Each file must be a 64-bit little-endian aarch64 ELF named lib*.so, every
PT_LOAD segment must be aligned for 16 KB pages, there must be no RPATH or
RUNPATH, and every NEEDED library must be either in DIR or one of the system
libraries every Android version provides. Exits 1 on any failure.
"""
import os
import struct
import sys

SYSTEM = {"libc.so", "libm.so", "libdl.so", "liblog.so"}
PT_LOAD, PT_DYNAMIC = 1, 2
DT_NEEDED, DT_STRTAB, DT_RPATH, DT_RUNPATH = 1, 5, 15, 29


def read(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("not a 64-bit little-endian ELF file")
    machine, = struct.unpack_from("<H", data, 18)
    if machine != 183:
        raise ValueError("not aarch64 (e_machine %d)" % machine)
    phoff, = struct.unpack_from("<Q", data, 32)
    phentsize, phnum = struct.unpack_from("<HH", data, 54)
    phdrs = []
    for i in range(phnum):
        p_type, _, p_offset, p_vaddr, _, p_filesz, _, p_align = struct.unpack_from(
            "<IIQQQQQQ", data, phoff + i * phentsize)
        phdrs.append((p_type, p_offset, p_vaddr, p_filesz, p_align))
    return data, phdrs


def dynamic(data, phdrs):
    """(tag, value) pairs of the dynamic section, and a reader for its strings."""
    loads = [p for p in phdrs if p[0] == PT_LOAD]

    def offset(vaddr):
        for _, off, va, size, _ in loads:
            if va <= vaddr < va + size:
                return off + vaddr - va
        raise ValueError("address 0x%x is in no segment" % vaddr)

    entries = []
    for p_type, off, _, size, _ in phdrs:
        if p_type != PT_DYNAMIC:
            continue
        for at in range(off, off + size, 16):
            tag, val = struct.unpack_from("<qQ", data, at)
            if tag == 0:
                break
            entries.append((tag, val))
    strtab = next((offset(v) for t, v in entries if t == DT_STRTAB), None)

    def string(index):
        end = data.index(b"\0", strtab + index)
        return data[strtab + index:end].decode()

    return entries, string


def main():
    folder = sys.argv[1]
    names = sorted(f for f in os.listdir(folder) if not f.startswith("."))
    shipped = set(names)
    problems = []
    for name in names:
        path = os.path.join(folder, name)
        if not (name.startswith("lib") and name.endswith(".so")):
            problems.append(f"{name}: not named lib*.so, so Android would not install it")
            continue
        try:
            data, phdrs = read(path)
            for p_type, _, vaddr, _, align in phdrs:
                if p_type == PT_LOAD and align < 16384:
                    problems.append(f"{name}: LOAD at 0x{vaddr:x} aligned to {align}, not 16 KB")
            entries, string = dynamic(data, phdrs)
            for tag, val in entries:
                if tag in (DT_RPATH, DT_RUNPATH):
                    problems.append(f"{name}: has RPATH/RUNPATH {string(val)}")
                elif tag == DT_NEEDED:
                    needed = string(val)
                    if needed not in shipped and needed not in SYSTEM:
                        problems.append(f"{name}: needs {needed}, which is not shipped")
        except (ValueError, struct.error) as e:
            problems.append(f"{name}: {e}")
    if problems:
        print("\n".join(problems))
        sys.exit(1)
    print(f"{len(names)} ELF files: aarch64, 16 KB aligned, no RUNPATH, every NEEDED present")


if __name__ == "__main__":
    main()
