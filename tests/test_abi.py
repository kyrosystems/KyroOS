#!/usr/bin/env python3
"""ABI integrity test.

1. Parses abi/syscalls.def and validates structural invariants
   (unique numbers, unique names, valid impl values, frozen range complete).
2. Runs the generator in --check mode: frozen numbers/names must be stable
   vs abi/abi_frozen.json.
3. Verifies generated src/include/sysnum.h is in sync with the registry
   (stale committed header = build would drift).
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gen_abi  # noqa: E402

FAILURES = []


def check(cond, msg):
    if cond:
        print(f"  ok: {msg}")
    else:
        FAILURES.append(msg)
        print(f"  FAIL: {msg}")


def main():
    entries = gen_abi.parse_registry()
    nums = [e["num"] for e in entries]
    names = [e["name"] for e in entries]

    print("[1/4] structural invariants")
    check(len(nums) == len(set(nums)), "syscall numbers are unique")
    check(len(names) == len(set(names)), "syscall names are unique")
    check(all(e["impl"] in ("yes", "partial", "no") for e in entries), "impl values valid")
    frozen = {e["num"]: e["name"] for e in entries if e["num"] <= 31}
    check(len(frozen) == 32, f"registry covers all frozen numbers 0-31 (got {len(frozen)})")
    expected_frozen = {
        0: "exit", 1: "write", 2: "open", 3: "close", 4: "read", 5: "stat",
        6: "mkdir", 7: "readdir", 8: "unlink", 9: "rmdir", 10: "socket",
        11: "connect", 12: "send", 13: "recv", 14: "bind", 15: "listen",
        16: "accept", 17: "get_ticks", 18: "sha256", 19: "malloc", 20: "free",
        21: "ioctl", 22: "mount", 23: "unmount", 24: "exec", 25: "gfx_get_fb_info",
        26: "input_poll_event", 27: "sbrk", 28: "sendto", 29: "recvfrom",
        30: "setsockopt", 31: "dns_resolve",
    }
    check(frozen == expected_frozen, "frozen 0-31 names match the historical KyroOS ABI")

    print("[2/4] frozen snapshot check (no renumbering)")
    r = subprocess.run([sys.executable, str(ROOT / "tools" / "gen_abi.py"), "--check"],
                       capture_output=True, text=True)
    check(r.returncode == 0, f"generator --check passes: {r.stdout.strip() or r.stderr.strip()}")

    print("[3/4] regenerate + diff")
    hdr_before = (ROOT / "src" / "include" / "sysnum.h").read_text() if (ROOT / "src" / "include" / "sysnum.h").exists() else ""
    subprocess.run([sys.executable, str(ROOT / "tools" / "gen_abi.py")], capture_output=True, text=True)
    hdr_after = (ROOT / "src" / "include" / "sysnum.h").read_text()
    check(hdr_before == hdr_after, "committed sysnum.h matches registry (run tools/gen_abi.py if not)")

    print("[4/4] header sanity")
    check("#define SYS_EXIT 0" in hdr_after, "SYS_EXIT is 0")
    check(f"#define KYROOS_NR_SYSCALLS {max(nums) + 1}" in hdr_after, "NR_SYSCALLS consistent")

    if FAILURES:
        print(f"\ntest_abi: {len(FAILURES)} failure(s)")
        return 1
    print("\ntest_abi: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())