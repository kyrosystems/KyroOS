# KyroOS System Installer (`/bin/installer`)

KyroOS includes an automated, interactive disk installation wizard that allows installing KyroOS directly onto physical or virtual storage drives (e.g., `/dev/sda`, `/dev/sdb`, `/dev/hda`).

## Features

- **Disk Detection**: Auto-detects available ATA/IDE and AHCI block devices via `/dev`.
- **MBR Partitioning**: Writes a standard Master Boot Record (MBR) and creates a primary bootable partition (`0x83` - KyroFS native).
- **Filesystem Formatting**: Formats the target partition with the KyroFS filesystem, setting up root directories, inode tables, and transaction journals.
- **System Installation**: Copies the kernel (`/boot/kernel.elf`), Limine bootloader files (`BOOTX64.EFI`, `limine.conf`, etc.), `/bin` userland utilities, and `/etc` system configuration files (`passwd`, `shadow`, `group`, `hostname`, `issue`).
- **Bootloader Configuration**: Configures Limine for direct BIOS or UEFI boot from the target disk.

## Usage

From the KyroOS shell (`/bin/sh`):
```sh
# Run the interactive installer
installer
```

The installer will guide you through:
1. Selecting the destination block device.
2. Confirming disk partition layout destruction.
3. Writing the MBR and partition table.
4. Formatting the target partition with KyroFS.
5. Populating system files.
6. Instructing you to remove the installation media and reboot into the installed system.
