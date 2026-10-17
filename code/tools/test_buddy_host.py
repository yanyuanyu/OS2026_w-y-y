#!/usr/bin/env python3
"""Compile the real freestanding allocator on the host; no model implementation."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "evidence" / "host"
OUT.mkdir(parents=True, exist_ok=True)
binary = OUT / "buddy_host"
subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-c",
                "tests/host_io.c", "-o", str(OUT / "host_io.o")], cwd=ROOT, check=True)
command = ["gcc", "-std=gnu99", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
           "-fno-builtin", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
           "-Ilibs", "-Ikern/mm", "-Ikern/debug", "tests/buddy_host.c",
           "kern/mm/buddy_pmm.c", str(OUT / "host_io.o"), "-o", str(binary)]
subprocess.run(command, cwd=ROOT, check=True)
result = subprocess.run([str(binary)], text=True, capture_output=True)
(OUT / "normal.log").write_text(result.stdout + result.stderr)
print(result.stdout + result.stderr, end="")
if result.returncode != 0:
    sys.exit(result.returncode)
for mode in "zdisromnuaf":
    result = subprocess.run([str(binary), mode], text=True, capture_output=True)
    (OUT / f"invalid-{mode}.log").write_text(result.stdout + result.stderr)
    if (result.returncode != 99 or "assertion failed:" not in result.stderr
            or "invalid operation was accepted" in result.stderr):
        raise SystemExit(f"invalid case {mode} not rejected correctly: {result}")
    print(f"HOST invalid-{mode}: expected assertion PASS")
print("HOST normal + 11 invalid cases PASS")
