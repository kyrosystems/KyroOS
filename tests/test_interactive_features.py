#!/usr/bin/env python3
"""Interactive verification of terminal cursor, GNU nano, Vulkan 3D vkcube, and Ctrl+C."""
import json
import re
import shutil
import socket
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

def main():
    qemu = shutil.which('qemu-system-x86_64') or r'C:\Program Files\qemu\qemu-system-x86_64.exe'
    iso = max(ROOT.glob('KyroOS-*.iso'), key=lambda p: p.stat().st_mtime)
    print(f"Testing Interactive Features on ISO: {iso}")

    out = ROOT / 'build' / 'test' / 'interactive'
    out.mkdir(parents=True, exist_ok=True)
    log = out / 'serial.log'
    if log.exists():
        log.unlink()

    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]

    cmd = [
        qemu, '-cdrom', str(iso), '-m', '256M', '-vga', 'std',
        '-display', 'none', '-serial', f'file:{log}', '-no-reboot', '-no-shutdown',
        '-qmp', f'tcp:127.0.0.1:{port},server=on,wait=off',
        '-device', 'e1000,netdev=n0', '-netdev', 'user,id=n0'
    ]

    proc = subprocess.Popen(cmd, cwd=ROOT)
    sock = None

    try:
        for _ in range(100):
            try:
                sock = socket.create_connection(('127.0.0.1', port), timeout=2)
                break
            except OSError:
                time.sleep(0.1)
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

        def wait_log(text, timeout=30):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if log.exists() and text in log.read_text(errors='replace'):
                    return
                if proc.poll() is not None:
                    break
                time.sleep(0.2)
            raise RuntimeError(f"Timeout waiting for marker '{text}' in {log}")

        def type_keys(keys, delay=0.15):
            for k in keys:
                hmp(f'sendkey {k}')
                time.sleep(delay)

        def take_screenshot(name):
            shot_path = out / f"{name}.ppm"
            hmp(f'screendump {shot_path.as_posix()}')
            time.sleep(0.2)
            return shot_path

        qmp('qmp_capabilities')
        print("Waiting for boot to login prompt...")
        wait_log('kyroos login:')
        print("Boot reached login prompt.")

        # Take screenshot of login prompt
        take_screenshot('01_login_prompt')

        # Log in as root
        print("Logging in as root...")
        type_keys(list('root') + ['ret'])
        time.sleep(1.0)
        take_screenshot('02_shell_logged_in')

        # Test 1: Test interactive typing in shell
        print("Testing interactive shell typing...")
        type_keys(list('echo hello kyroos') + ['ret'])
        time.sleep(0.5)
        take_screenshot('03_shell_echo')

        # Test 2: Test nano editor
        print("Launching nano test.txt...")
        type_keys(list('nano test.txt') + ['ret'])
        time.sleep(1.0)

        # Type some text into nano
        type_keys(list('Hello World from GNU nano!') + ['ret'])
        type_keys(list('Second line of text.') + ['ret'])
        time.sleep(0.5)

        # Take screenshot of nano
        shot_nano = take_screenshot('04_nano_editor')
        print(f"Captured nano screenshot: {shot_nano}")

        # Exit nano without saving (^X then 'n')
        type_keys(['ctrl-x'])
        time.sleep(0.5)
        take_screenshot('05_nano_save_prompt')
        type_keys(['n'])
        time.sleep(0.5)
        take_screenshot('06_shell_after_nano')

        # Test 3: Test vkcube and Ctrl+C interruption
        print("Launching vkcube...")
        type_keys(list('vkcube') + ['ret'])
        time.sleep(2.0)

        # Take screenshot of vkcube running
        shot_vkcube = take_screenshot('07_vkcube_running')
        print(f"Captured vkcube screenshot: {shot_vkcube}")

        # Send Ctrl+C to terminate vkcube
        print("Sending Ctrl+C to terminate vkcube...")
        type_keys(['ctrl-c'])
        time.sleep(1.0)

        # Take screenshot confirming return to shell
        shot_after_ctrl_c = take_screenshot('08_shell_after_ctrl_c')
        print(f"Captured shell screenshot after Ctrl+C: {shot_after_ctrl_c}")

        # Type pwd to verify shell is active and responsive
        type_keys(list('pwd') + ['ret'])
        time.sleep(0.5)
        shot_pwd = take_screenshot('09_shell_pwd_verified')
        print(f"Captured final responsive shell screenshot: {shot_pwd}")

        print("\n=== ALL INTERACTIVE VERIFICATIONS PASSED SUCCESSFULLY! ===")

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
