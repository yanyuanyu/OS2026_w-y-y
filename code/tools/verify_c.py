#!/usr/bin/env python3
"""Fresh builds and bounded QEMU runs. Old supplied evidence is never read."""
import datetime
import json
import os
import re
from pathlib import Path
import selectors
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "evidence" / "qemu"


def run_case(backend, memory, run_id):
    image = ROOT / "bin" / ("" if backend == "bestfit" else backend) / "ucore.img"
    log = OUT / f"{backend}-{memory}MiB.log"
    command = ["qemu-system-riscv64", "-machine", "virt", "-m", str(memory),
               "-smp", "1", "-nographic", "-bios", "default", "-monitor", "none",
               "-kernel", str(image)]
    with log.open("w") as stream:
        stream.write(f"# run_id={run_id}\n")
        stream.flush()
        proc = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        selector = selectors.DefaultSelector()
        selector.register(proc.stdout, selectors.EVENT_READ)
        text = ""
        try:
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                if selector.select(timeout=.1):
                    chunk = os.read(proc.stdout.fileno(), 65536).decode(errors="replace")
                    stream.write(chunk)
                    stream.flush()
                    text += chunk
                if "panic" in text.lower():
                    raise RuntimeError(f"kernel panic: {log}")
                # A pipe read can end midway through a printf. Require the
                # complete final line so archived evidence is not truncated.
                physical = re.search(r"satp physical address: (0x[0-9a-fA-F]+)\r?\n", text)
                if physical:
                    if "check_alloc_page() succeeded!" not in text:
                        raise RuntimeError(f"PMM check missing: {log}")
                    if "slub_check() succeeded! free pages restored:" not in text:
                        raise RuntimeError(f"SLUB check missing: {log}")
                    expected = {"bestfit": "best_fit", "firstfit": "default", "buddy": "buddy"}
                    if f"memory management: {expected[backend]}_pmm_manager" not in text:
                        raise RuntimeError(f"wrong backend: {log}")
                    if backend == "buddy" and "C-BUDDY ALL PASS" not in text:
                        raise RuntimeError(f"Buddy check missing: {log}")
                    if backend == "buddy" and "C-PROBE store pair PASS" not in text:
                        raise RuntimeError(f"store probe check missing: {log}")
                    virtual = re.search(r"satp virtual address: (0x[0-9a-fA-F]+)", text)
                    if virtual is None or int(physical[1], 16) != int(virtual[1], 16) - 0xffffffff40000000:
                        raise RuntimeError(f"invalid boot page-table address: {log}")
                    return {"backend": backend, "memory_MiB": memory,
                            "result": "PASS", "command": command, "log": str(log.relative_to(ROOT))}
                if proc.poll() is not None:
                    raise RuntimeError(f"QEMU exited early ({proc.returncode}): {log}")
            raise TimeoutError(f"QEMU never reached completion: {log}")
        finally:
            selector.close()
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            proc.stdout.close()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    state = {"run_id": str(uuid.uuid4()), "status": "RUNNING", "results": []}
    summary = OUT / "summary.json"

    def save():
        temporary = OUT / "summary.json.tmp"
        temporary.write_text(json.dumps(state, indent=2))
        temporary.replace(summary)

    save()  # Invalidate an earlier PASS before touching any prior run's logs.
    try:
        environment = {"utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                       "run_id": state["run_id"]}
        for command in (["riscv64-unknown-elf-gcc", "--version"],
                        ["qemu-system-riscv64", "--version"], ["uname", "-a"]):
            environment[command[0]] = subprocess.check_output(command, text=True).splitlines()[0]
        (OUT / "environment.json").write_text(json.dumps(environment, indent=2))
        for backend in ("bestfit", "firstfit", "buddy"):
            with (OUT / f"{backend}-build.log").open("w") as stream:
                stream.write(f"# run_id={state['run_id']}\n")
                stream.flush()
                subprocess.run(["make", f"PMM_ALGO={backend}", "clean"], cwd=ROOT,
                               stdout=stream, stderr=subprocess.STDOUT, check=True)
                subprocess.run(["make", "-j2", f"PMM_ALGO={backend}"], cwd=ROOT,
                               stdout=stream, stderr=subprocess.STDOUT, check=True)
            for memory in ((64, 128, 256) if backend == "buddy" else (128,)):
                state["results"].append(run_case(backend, memory, state["run_id"]))
                save()
                print(f"QEMU {backend} {memory}MiB PASS", flush=True)
    except BaseException as exc:
        state.update(status="FAIL", error=str(exc))
        save()
        raise
    state["status"] = "PASS"
    save()


if __name__ == "__main__":
    main()
