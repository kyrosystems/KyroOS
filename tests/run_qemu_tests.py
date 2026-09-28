#!/usr/bin/env python3
"""KyroOS integration test runner.

Boots the newest KyroOS ISO headless in QEMU, waits for boot + selftest
completion, and parses the serial log for SELFTEST lines:

    SELFTEST: <name>: PASS|FAIL
    SELFTEST_SUMMARY: <passed>/<total>

Usage:  python3 tests/run_qemu_tests.py [--iso <path>] [--timeout 120]

Exit code: 0 = all PASS, 1 = failures, 2 = environment error.
"""
import argparse
import glob
import os
import re
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def find_qemu():
    for cand in ("qemu-system-x86_64", str(Path.home() / "tools/usr/bin/qemu-system-x86_64")):
        if subprocess.run(["which", cand] if "/" not in cand else ["test", "-x", cand],
                          capture_output=True).returncode == 0:
            return cand
    sys.exit(2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=None)
    ap.add_argument("--timeout", type=int, default=120)
    args = ap.parse_args()

    qemu = find_qemu()
    env = dict(os.environ)
    qdata = Path.home() / "tools/usr/share/qemu"
    if qdata.exists():
        env["QEMU_MODULE_DIR"] = str(Path.home() / "tools/usr/lib/x86_64-linux-gnu/qemu")
        env["LD_LIBRARY_PATH"] = str(Path.home() / "tools/usr/lib/x86_64-linux-gnu") + ":" + env.get("LD_LIBRARY_PATH", "")

    iso = args.iso
    if not iso:
        isos = sorted(glob.glob(str(ROOT / "KyroOS-*.iso")), key=os.path.getmtime)
        if not isos:
            print("No ISO found; build with `make iso` first.")
            return 2
        iso = isos[-1]
    print(f"Testing ISO: {iso}")

    work = Path("/tmp") / f"kyroos-test-{os.getpid()}"
    work.mkdir(exist_ok=True)
    log = work / "boot.log"
    mon = work / "mon.sock"

    cmd = [qemu, "-L", str(qdata) if qdata.exists() else "", "-cdrom", iso,
           "-serial", f"file:{log}", "-display", "none", "-no-reboot", "-m", "128M",
           "-device", "e1000,netdev=net0",
           "-netdev", "user,id=net0,hostname=kyroos,dns=8.8.8.8",
           "-monitor", f"unix:{mon},server,nowait"]
    cmd = [c for c in cmd if c]

    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
                            cwd=str(ROOT), env=env, preexec_fn=os.setsid)
    results = []
    summary = None
    try:
        deadline = time.time() + args.timeout
        while time.time() < deadline:
            if log.exists():
                text = log.read_text(errors="replace")
                for m in re.finditer(r"SELFTEST: ([a-z0-9_]+): (PASS|FAIL)(?: \(([^)]*)\))?", text):
                    name, ok, detail = m.groups()
                    if not any(r[0] == name for r in results):
                        results.append((name, ok, detail))
                        print(f"  {ok:4} {name} {('(' + detail + ')') if detail else ''}")
                sm = re.search(r"SELFTEST_SUMMARY: (\d+)/(\d+)", text)
                if sm:
                    summary = sm.groups()
                    break
            if proc.poll() is not None:
                break
            time.sleep(1)
    finally:
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        except Exception:
            pass

    if not results:
        print("FAIL: no SELFTEST output found in serial log (boot failed?)")
        return 1

    failed = [r for r in results if r[1] != "PASS"]
    if failed or (summary and summary[0] != summary[1]):
        print(f"\nRESULT: FAIL ({len(failed)} failed of {len(results)})")
        return 1
    print(f"\nRESULT: PASS ({len(results)}/{len(results)} checks passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())