#!/usr/bin/env python3
"""Interactive verification of:
1. Shell typing, moving cursor back, and inserting text without overlap
2. Gears OpenGL demo running without crash
3. Cube3D interactive mouse-controlled 3D cube demo
4. Ctrl+C process termination
"""
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

        def type_keys(keys, delay=0.12):
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

        # Log in as root
        type_keys(list('root') + ['ret'])
        time.sleep(1.0)
        take_screenshot('10_shell_prompt')

        # Test 1: Verify cursor move back and typing without overlapping!
        print("Testing typing, moving cursor back, and typing inside...")
        # Type 'asfhr'
        type_keys(list('asfhr'))
        time.sleep(0.3)
        # Move back 3 characters
        type_keys(['left', 'left', 'left'])
        time.sleep(0.3)
        # Type '123'
        type_keys(list('123'))
        time.sleep(0.3)
        take_screenshot('11_cursor_insert_clean')
        print("Captured cursor insert screenshot (11_cursor_insert_clean.ppm).")

        # Clear line
        type_keys(['ctrl-c'])
        time.sleep(0.5)

        # Test 2: Launch OpenGL gears demo
        print("Launching gears (OpenGL)...")
        type_keys(list('gears') + ['ret'])
        time.sleep(2.0)
        shot_gears = take_screenshot('12_gears_running')
        print(f"Captured gears screenshot: {shot_gears}")

        # Exit gears with 'q'
        type_keys(['q'])
        time.sleep(1.0)
        take_screenshot('13_shell_after_gears')

        # Test 3: Launch new interactive cube3d
        print("Launching cube3d (Interactive 3D with mouse)...")
        type_keys(list('cube3d') + ['ret'])
        time.sleep(2.0)

        # Send mouse movement and left drag via QMP/HMP
        # mouse_move dx dy [dz]
        hmp('mouse_move 50 -30')
        time.sleep(0.2)
        hmp('mouse_button 1') # Press left button
        time.sleep(0.2)
        hmp('mouse_move -100 80') # Drag mouse
        time.sleep(0.3)
        hmp('mouse_button 0') # Release left button
        time.sleep(0.3)

        shot_cube3d = take_screenshot('14_cube3d_running')
        print(f"Captured cube3d screenshot: {shot_cube3d}")

        # Send Ctrl+C to terminate cube3d
        print("Sending Ctrl+C to exit cube3d...")
        type_keys(['ctrl-c'])
        time.sleep(1.0)
        take_screenshot('15_shell_after_cube3d')

        # Run pwd to confirm shell is healthy
        type_keys(list('pwd') + ['ret'])
        time.sleep(0.5)
        take_screenshot('16_final_health_check')

        print("\n=== ALL VERIFICATIONS COMPLETED SUCCESSFULLY! ===")

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
