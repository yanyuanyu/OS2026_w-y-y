#!/usr/bin/env python3
from pathlib import Path
import tempfile, shutil, subprocess, sys
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="lab4-c-") as tmp:
    dest = Path(tmp) / "code"
    shutil.copytree(root, dest, ignore=shutil.ignore_patterns("obj", "bin", ".git"))
    p = dest / "kern/process/proc.c"
    text = p.read_text()
    text = text.replace("static int\ninit_main(void *arg)", '#include "../../tests/c/kernel_checks.c"\n\nstatic int\ninit_main(void *arg)')
    text = text.replace('    cprintf("this initproc', '    c_test_main();\n    cprintf("this initproc', 1)
    text = text.replace("        if (current->need_resched) schedule();", "        c_idle_hook();\n        if (current->need_resched) schedule();", 1)
    p.write_text(text)
    shutil.copy(dest / "tests/c/register_check.S", dest / "kern/process/c_register_check.S")
    subprocess.run(["make", "-s", "-j2"], cwd=dest, check=True, stdout=subprocess.DEVNULL)
    p = subprocess.run(["qemu-system-riscv64", "-machine", "virt", "-nographic", "-bios", "default", "-kernel", "bin/ucore.img"], cwd=dest, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
    output = p.stdout.decode(errors="replace")
    print(output, end="")
    if p.returncode or "[C TEST] ALL PASS" not in output: sys.exit(1)
    print("[HOST] isolated QEMU exit=0; production source unchanged")
