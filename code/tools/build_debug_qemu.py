#!/usr/bin/env python3
"""Build a separate QEMU 4.1.1 with symbols, without installing over system QEMU."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--work", type=Path, default=Path.home() / "lab2-c-qemu-4.1.1")
args = parser.parse_args()
work = args.work.resolve()
work.mkdir(parents=True, exist_ok=True)
archive = work / "qemu-4.1.1.tar.xz"
if not archive.exists():
    urllib.request.urlretrieve("https://download.qemu.org/qemu-4.1.1.tar.xz", archive)
source = work / "qemu-4.1.1"
if not source.exists():
    subprocess.run(["tar", "-xJf", str(archive), "-C", str(work)], check=True)
build = work / "build"
build.mkdir(exist_ok=True)
out = ROOT / "evidence" / "host-qemu"
out.mkdir(parents=True, exist_ok=True)
configure = [str(source / "configure"), "--target-list=riscv64-softmmu", "--enable-debug",
             "--disable-werror", "--disable-strip", "--disable-docs", "--disable-gtk",
             "--disable-sdl", "--disable-vnc", "--disable-curses", "--extra-cflags=-O0"]
with (out / "build.log").open("w") as log:
    subprocess.run(configure, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(["make", f"-j{min(os.cpu_count() or 2, 4)}"], cwd=build,
                   stdout=log, stderr=subprocess.STDOUT, check=True)
binary = build / "riscv64-softmmu" / "qemu-system-riscv64"
version = subprocess.check_output([str(binary), "--version"], text=True)
record = {"source": str(source), "binary": str(binary), "configure": configure,
          "version": version, "download_url": "https://download.qemu.org/qemu-4.1.1.tar.xz",
          "download_sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}
(out / "build.json").write_text(json.dumps(record, indent=2))
print(version, end="")
print(f"Debug QEMU ready: {binary}")
