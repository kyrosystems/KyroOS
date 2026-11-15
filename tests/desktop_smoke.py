#!/usr/bin/env python3
"""Boot/login/desktop smoke test using QMP (Windows and Linux)."""
import argparse
import json
import re
import shutil
import socket
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--qemu', default=shutil.which('qemu-system-x86_64') or r'C:\Program Files\qemu\qemu-system-x86_64.exe')
    ap.add_argument('--iso', type=Path)
    ap.add_argument('--vga', default='std')
    ap.add_argument('--user', choices=['root', 'user'], default='root')
    ap.add_argument('--memory', default='256M')
    ap.add_argument('--trace', action='store_true')
    ap.add_argument('--uefi-code', type=Path)
    ap.add_argument('--uefi-vars', type=Path)
    args = ap.parse_args()
    iso = args.iso or max(ROOT.glob('KyroOS-*.iso'), key=lambda p: p.stat().st_mtime)
    out = ROOT / 'build' / 'test' / (args.vga + ('-user' if args.user == 'user' else '') + ('-uefi' if args.uefi_code else ''))
    out.mkdir(parents=True, exist_ok=True)
    log = out / 'serial.log'
    shot = out / 'desktop.ppm'
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    cmd = [args.qemu, '-cdrom', str(iso), '-m', args.memory, '-vga', args.vga,
           '-display', 'none', '-serial', f'file:{log}', '-no-reboot', '-no-shutdown',
           '-qmp', f'tcp:127.0.0.1:{port},server=on,wait=off',
           '-device', 'e1000,netdev=n0', '-netdev', 'user,id=n0']
    if args.uefi_code:
        if not args.uefi_vars:
            ap.error('--uefi-code requires --uefi-vars')
        variables = out / 'uefi-vars.fd'
        shutil.copyfile(args.uefi_vars, variables)
        cmd += ['-drive', f'if=pflash,unit=0,format=raw,readonly=on,file={args.uefi_code}',
                '-drive', f'if=pflash,unit=1,format=raw,file={variables}']
    if args.trace:
        cmd += ['-d', 'int,cpu_reset', '-D', str(out / 'cpu.log')]
    proc = subprocess.Popen(cmd, cwd=ROOT)
    sock = None
    try:
        for _ in range(100):
            try:
                sock = socket.create_connection(('127.0.0.1', port), timeout=2)
                break
            except OSError:
                time.sleep(.1)
        if sock is None:
            raise RuntimeError('QMP unavailable')
        stream = sock.makefile('rwb')
        stream.readline()

        def qmp(command, arguments=None):
            stream.write((json.dumps({'execute': command, 'arguments': arguments or {}}) + '\n').encode())
            stream.flush()
            while True:
                reply = json.loads(stream.readline())
                if 'error' in reply:
                    raise RuntimeError(reply)
                if 'return' in reply:
                    return reply['return']

        def hmp(command):
            return qmp('human-monitor-command', {'command-line': command})

        def wait_log(text, timeout=45, count=1):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if log.exists() and log.read_text(errors='replace').count(text) >= count:
                    return
                if proc.poll() is not None:
                    break
                time.sleep(.2)
            raise RuntimeError(f'Missing serial marker: {text}; see {log}')

        def type_keys(keys):
            for key in keys:
                hmp('sendkey ' + key)
                time.sleep(.15)

        qmp('qmp_capabilities')
        wait_log('SELFTEST_SUMMARY:')
        summary = re.search(r'SELFTEST_SUMMARY: (\d+)/(\d+)', log.read_text(errors='replace'))
        assert summary and int(summary[1]) > 0 and summary[1] == summary[2], 'Boot selftests failed'
        wait_log('kyroos login:')
        type_keys(list(args.user) + ['ret'])
        wait_log('DESKTOP: session ready')
        time.sleep(3)
        hmp('screendump ' + shot.as_posix())
        data = shot.read_bytes()
        assert data.startswith(b'P6'), 'Screenshot unavailable'
        assert len(set(data[-100000:])) > 16, 'Desktop appears blank'
        # Exercise several applications and the keyboard window manager.
        for key, name in [('f2', 'files'), ('f3', 'editor'), ('f4', 'tasks'), ('f5', 'settings')]:
            type_keys([key])
            time.sleep(.4)
            app_shot = out / (name + '.ppm')
            hmp('screendump ' + app_shot.as_posix())
            app_data = app_shot.read_bytes()
            assert sum(a != b for a, b in zip(data, app_data)) > 10000, f'{name} did not render'
        type_keys(['f1'])
        # Run a guest ABI test from the focused terminal.
        type_keys(list('posixcheck') + ['ret'])
        wait_log('POSIXCHECK: PASS')
        time.sleep(2)
        text = log.read_text(errors='replace')
        assert 'DOUBLE FAULT' not in text and 'EMERGENCY EXCEPTION' not in text
        assert '[Userspace crash]' not in text
        hmp('screendump ' + (out / 'posixcheck.ppm').as_posix())
        type_keys(['esc', 'l'])
        wait_log('session terminated cleanly')
        wait_log('kyroos login:', count=2)
        type_keys(list('root') + ['ret'])
        wait_log('DESKTOP: session ready', count=2)
        print(f'PASS: boot, {args.user} login, desktop/apps, guest ABI, logout and root re-login ({args.vga}, {args.memory})')
        print(f'Artifacts: {out}')
    finally:
        if sock:
            sock.close()
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()


if __name__ == '__main__':
    main()
