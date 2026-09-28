#!/usr/bin/env python3
"""KyroOS syscall ABI registry generator.

Reads abi/syscalls.def (single source of truth) and emits:
  - src/include/sysnum.h      (SYS_* numbers + SYS_IMPL_* flags, shared kernel/userspace)
  - abi/syscalls.md           (human-readable ABI documentation)

Usage:
  python3 tools/gen_abi.py            # generate headers + docs
  python3 tools/gen_abi.py --check    # verify frozen numbers did not change
  python3 tools/gen_abi.py --freeze   # (re)record frozen ABI snapshot

Checks enforced:
  * numbers are unique
  * numbers 0-31 (frozen range) keep their names (abi frozen snapshot)
  * impl=no entries are still assigned numbers (reserved, never reallocated)
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEF = ROOT / "abi" / "syscalls.def"
FROZEN = ROOT / "abi" / "abi_frozen.json"
OUT_HDR = ROOT / "src" / "include" / "sysnum.h"
OUT_MD = ROOT / "abi" / "syscalls.md"

FROZEN_MAX = 31  # numbers 0..31 are frozen forever (compat with existing binaries)


def parse_registry():
    entries = []
    seen_nums = {}
    seen_names = {}
    for lineno, raw in enumerate(DEF.read_text().splitlines(), 1):
        line = raw.split("#", 1)[0].strip() if not raw.strip().startswith("#") else ""
        if not line.strip() or line.strip().startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) < 7:
            sys.exit(f"abi/syscalls.def:{lineno}: expected >=7 fields, got {len(parts)}: {raw}")
        num_s, name, ret, args, caps, impl, desc = parts[:7]
        try:
            num = int(num_s)
        except ValueError:
            sys.exit(f"abi/syscalls.def:{lineno}: bad number {num_s!r}")
        if impl not in ("yes", "partial", "no"):
            sys.exit(f"abi/syscalls.def:{lineno}: bad impl {impl!r}")
        if num in seen_nums:
            sys.exit(f"abi/syscalls.def:{lineno}: duplicate number {num} (also {seen_nums[num]})")
        if name in seen_names:
            sys.exit(f"abi/syscalls.def:{lineno}: duplicate name {name!r}")
        seen_nums[num] = name
        seen_names[name] = num
        entries.append(dict(num=num, name=name, ret=ret, args=args, caps=caps, impl=impl, desc=desc))
    return entries


def macro(name):
    return "SYS_" + name.upper()


def gen_header(entries):
    max_num = max(e["num"] for e in entries)
    lines = [
        "/* GENERATED FILE - DO NOT EDIT BY HAND",
        " * Source of truth: abi/syscalls.def",
        " * Regenerate: python3 tools/gen_abi.py",
        " * Mechanism: int 0x80; args RDI,RSI,RDX,R10,R8,R9; result RAX.",
        " * Errors: kernel returns -errno (RAX < 0 signed); libc wrapper sets errno, returns -1.",
        " */",
        "#ifndef SYSNUM_H",
        "#define SYSNUM_H",
        "",
    ]
    impl_lines = []
    for e in entries:
        lines.append(f"#define {macro(e['name'])} {e['num']}")
        impl_lines.append((e["num"], macro(e["name"]), 1 if e["impl"] == "yes" else 0))
    lines.append("")
    lines.append(f"#define KYROOS_NR_SYSCALLS {max_num + 1}")
    lines.append("")
    lines.append("/* impl flags: 1 = fully implemented, 0 = partial/not implemented */")
    for num, m, flag in impl_lines:
        lines.append(f"#define {m}_IMPL {flag}")
    lines.append("")
    lines.append("#endif /* SYSNUM_H */")
    return "\n".join(lines) + "\n"


def gen_md(entries):
    out = [
        "# KyroOS Syscall ABI (generated from abi/syscalls.def)",
        "",
        "Mechanism: `int 0x80`, number in `RAX`, args in `RDI,RSI,RDX,R10,R8,R9`,",
        "result in `RAX`. Errors: kernel returns `-errno`; libc wrapper sets `errno`",
        "and returns `-1`.",
        "",
        "| # | name | ret | args | caps | impl | description |",
        "|---|------|-----|------|------|------|-------------|",
    ]
    for e in entries:
        args = e["args"].replace("|", "\\|")
        desc = e["desc"].replace("|", "\\|")
        out.append(f"| {e['num']} | `{e['name']}` | `{e['ret']}` | `{args}` | {e['caps']} | {e['impl']} | {desc} |")
    out.append("")
    out.append("Statuses: `yes` = implemented and covered by tests; `partial` = implemented "
               "with documented gaps; `no` = reserved number, planned work (never reallocate).")
    return "\n".join(out) + "\n"


def snapshot(entries):
    return {str(e["num"]): e["name"] for e in entries}


def main():
    entries = parse_registry()

    frozen_max_entries = [e for e in entries if e["num"] <= FROZEN_MAX]
    have_frozen = set(range(0, FROZEN_MAX + 1))
    got_frozen = {e["num"] for e in frozen_max_entries}
    if have_frozen - got_frozen:
        sys.exit(f"FATAL: frozen numbers missing from registry: {sorted(have_frozen - got_frozen)}")

    if "--freeze" in sys.argv:
        FROZEN.write_text(json.dumps(snapshot(entries), indent=1, sort_keys=True) + "\n")
        print(f"Frozen ABI snapshot written: {FROZEN} ({len(snapshot(entries))} numbers)")
    if "--check" in sys.argv:
        if not FROZEN.exists():
            sys.exit(f"FATAL: {FROZEN} missing; run --freeze once")
        old = json.loads(FROZEN.read_text())
        cur = snapshot(entries)
        problems = []
        for num, name in old.items():
            if cur.get(num) != name:
                problems.append(f"number {num}: was {name}, now {cur.get(num)!r} — ABI BREAK")
        if problems:
            sys.exit("FATAL: frozen ABI violated:\n  " + "\n  ".join(problems))
        print(f"ABI check OK: {len(old)} frozen numbers unchanged.")

    OUT_HDR.write_text(gen_header(entries))
    OUT_MD.write_text(gen_md(entries))
    print(f"Generated {OUT_HDR.relative_to(ROOT)} and {OUT_MD.relative_to(ROOT)} "
          f"({len(entries)} syscalls, {sum(1 for e in entries if e['impl']=='yes')} impl=yes)")


if __name__ == "__main__":
    main()