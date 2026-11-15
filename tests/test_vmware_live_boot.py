#!/usr/bin/env python3
"""Host regressions for the non-destructive VMware live-boot fix."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('vmware_live_boot', ROOT / 'tools/vmware_live_boot.py')
boot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(boot)

CONFIG = ('ide0:0.present = "TRUE"\r\n'
          'ide0:0.fileName = "valuable-disk.vmdk"\r\n'
          'ide1:0.deviceType = "cdrom-image"\r\n'
          'ide1:0.fileName = "old.iso"\r\n'
          'serial0.fileName = "serial.log"\r\n'
          'numvcpus = "4"\r\n')


class LiveBootTests(unittest.TestCase):
    def test_preserve_disks_and_hardware(self):
        fixed = boot.live_boot_config(CONFIG, 'new.iso')
        for line in CONFIG.splitlines():
            if not line.startswith('ide1:0.fileName'):
                self.assertIn(line, fixed)
        self.assertEqual(boot.get_value(fixed, 'bios.bootOrder'), 'cdrom,hdd')
        self.assertEqual(boot.get_value(fixed, 'ide1:0.startConnected'), 'TRUE')
        self.assertEqual(boot.get_value(fixed, 'ide1:0.fileName'), 'new.iso')
        self.assertNotIn('\n', fixed.replace('\r\n', ''))

    def test_idempotent(self):
        fixed = boot.live_boot_config(CONFIG, 'new.iso')
        self.assertEqual(fixed, boot.live_boot_config(fixed, 'new.iso'))

    def test_existing_order(self):
        config = CONFIG + 'bios.bootOrder = "hdd,ethernet,cdrom"\r\n'
        fixed = boot.live_boot_config(config, 'new.iso')
        self.assertEqual(boot.get_value(fixed, 'bios.bootOrder'), 'cdrom,hdd,ethernet')

    def test_refuse_efi(self):
        with self.assertRaises(ValueError):
            boot.live_boot_config(CONFIG + 'firmware = "efi"\r\n', 'new.iso')

    def test_refuse_disk_as_cd(self):
        with self.assertRaises(ValueError):
            boot.live_boot_config(CONFIG.replace('cdrom-image', 'disk'), 'new.iso')

    def test_refuse_duplicate_keys(self):
        with self.assertRaises(ValueError):
            boot.live_boot_config(CONFIG + 'ide1:0.fileName = "other.iso"\r\n', 'new.iso')

    def test_refuse_injection(self):
        with self.assertRaises(ValueError):
            boot.live_boot_config(CONFIG, 'bad.iso"\nide0:0.present = "FALSE')

    def test_backup_and_noop(self):
        with tempfile.TemporaryDirectory() as work:
            vmx = Path(work) / 'test.vmx'
            original = CONFIG.encode('latin-1')
            updated = boot.live_boot_config(CONFIG, 'new.iso').encode('latin-1')
            vmx.write_bytes(original)
            backup = boot.apply_config(vmx, original, updated)
            self.assertEqual(backup.read_bytes(), original)
            self.assertEqual(vmx.read_bytes(), updated)
            self.assertIsNone(boot.apply_config(vmx, updated, updated))

    def test_existing_temporary_is_preserved(self):
        with tempfile.TemporaryDirectory() as work:
            vmx = Path(work) / 'test.vmx'
            vmx.write_bytes(CONFIG.encode())
            temp = vmx.with_name('test.vmx.live-boot.tmp')
            temp.write_bytes(b'another operation')
            with self.assertRaises(FileExistsError):
                boot.apply_config(vmx, CONFIG.encode(), b'updated')
            self.assertEqual(temp.read_bytes(), b'another operation')
            self.assertEqual(vmx.read_bytes(), CONFIG.encode())

    def test_refuse_concurrent_edit(self):
        with tempfile.TemporaryDirectory() as work:
            vmx = Path(work) / 'test.vmx'
            vmx.write_bytes(b'changed by GUI')
            with self.assertRaises(RuntimeError):
                boot.apply_config(vmx, CONFIG.encode(), b'updated')
            self.assertEqual(vmx.read_bytes(), b'changed by GUI')


if __name__ == '__main__':
    unittest.main()
