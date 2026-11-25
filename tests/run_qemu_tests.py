#!/usr/bin/env python3
"""Boot the latest ISO and require a complete passing SELFTEST summary."""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--iso', type=Path)
    ap.add_argument('--timeout', type=int, default=120)
    ap.add_argument('--qemu', default=shutil.which('qemu-system-x86_64'))
    args = ap.parse_args()
    candidates = [args.qemu, str(Path.home() / 'tools/usr/bin/qemu-system-x86_64'),
                  r'C:\Program Files\qemu\qemu-system-x86_64.exe']
    qemu = next((p for p in candidates if p and Path(p).is_file()), None)
    if not qemu:
        print('QEMU not found; specify --qemu <executable>.')
        return 2
    isos = list(ROOT.glob('KyroOS-*.iso'))
    if not args.iso and not isos:
        print('No ISO found; build with make first.')
        return 2
    iso = args.iso or max(isos, key=lambda p: p.stat().st_mtime)
    print(f'Testing ISO: {iso}')
    env = dict(os.environ)
    qdata = Path.home() / 'tools/usr/share/qemu'
    cmd = [qemu, '-cdrom', str(iso.resolve()), '-display', 'none', '-no-reboot',
           '-m', '128M', '-device', 'e1000,netdev=net0', '-netdev', 'user,id=net0']
    if qdata.exists() and os.name != 'nt':
        cmd += ['-L', str(qdata)]
        libs = Path.home() / 'tools/usr/lib/x86_64-linux-gnu'
        env['QEMU_MODULE_DIR'] = str(libs / 'qemu')
        env['LD_LIBRARY_PATH'] = str(libs) + ':' + env.get('LD_LIBRARY_PATH', '')
    with tempfile.TemporaryDirectory(prefix='kyroos-test-') as work:
        log = Path(work) / 'boot.log'
        cmd += ['-serial', f'file:{log}']
        proc = subprocess.Popen(cmd, cwd=ROOT, env=env)
        text = ''
        summary = None
        try:
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                if log.exists():
                    text = log.read_text(errors='replace')
                    summary = re.search(r'SELFTEST_SUMMARY: (\d+)/(\d+)', text)
                    if summary:
                        break
                if proc.poll() is not None:
                    break
                time.sleep(.2)
        finally:
            if proc.poll() is None:
                proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        results = re.findall(r'SELFTEST: ([a-z0-9_]+): (PASS|FAIL)', text)
        for name, result in results:
            print(f'  {result}: {name}')
        if not summary:
            print('FAIL: incomplete boot/selftest; last serial output:')
            print(text[-4000:])
            return 1
        passed, total = map(int, summary.groups())
        ok = total > 0 and passed == total and len(results) == total and all(r == 'PASS' for _, r in results)
        print(f'RESULT: {"PASS" if ok else "FAIL"} ({passed}/{total})')
        return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
