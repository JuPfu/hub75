#!/usr/bin/env python3
"""Print flash/RAM usage of a built ELF as bytes and % of the linked memory regions."""
import argparse
import re
import subprocess
import sys


def region_sizes(map_path):
    with open(map_path) as f:
        text = f.read()
    m = re.search(r"Memory Configuration\n\n.*?\n((?:.+\n)+?)\n", text)
    if not m:
        sys.exit(f"could not find Memory Configuration in {map_path}")
    regions = {}
    for line in m.group(1).splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0] not in ("*default*",):
            name, origin, length = parts[0], parts[1], parts[2]
            if re.match(r"^0x[0-9a-fA-F]+$", origin) and re.match(r"^0x[0-9a-fA-F]+$", length):
                regions[name] = int(length, 16)
    return regions


def elf_sizes(size_tool, elf_path):
    out = subprocess.check_output([size_tool, "--format=berkeley", elf_path], text=True)
    fields = out.strip().splitlines()[-1].split()
    text, data, bss = int(fields[0]), int(fields[1]), int(fields[2])
    return text, data, bss


def fmt(used, total):
    pct = 100.0 * used / total if total else 0.0
    return f"{used:>8} / {total:<8} bytes ({pct:5.1f}%)"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", required=True)
    ap.add_argument("--map", required=True)
    ap.add_argument("--size-tool", required=True)
    args = ap.parse_args()

    text, data, bss = elf_sizes(args.size_tool, args.elf)
    regions = region_sizes(args.map)

    flash_total = regions.get("FLASH", 0)
    ram_total = regions.get("RAM", 0)

    print(f"Flash: {fmt(text + data, flash_total)}")
    print(f"RAM:   {fmt(data + bss, ram_total)}")


if __name__ == "__main__":
    main()
