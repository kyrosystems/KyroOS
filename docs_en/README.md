# KyroOS documentation (dev)

This directory documents the `dev` branch. Start with the [current build and run guide](build_and_run_dev.md). The older [build.md](build.md) predates the current Makefile and may contain outdated commands and artifact names.

## Topics

- [Overview](overview.md), [kernel](kernel.md), [boot](boot.md), [memory](memory.md), [interrupts](interrupts.md).
- [Userspace](userspace.md), [system calls](syscalls.md), [writing applications](writing_applications.md), [porting](porting_applications.md).
- [Filesystem](filesystem.md), [IPC](ipc.md), [drivers](drivers.md), [networking](networking.md).
- [Graphics](graphics.md), [graphics applications](graphics_applications.md), [graphics guide](graphics_guide.md).
- [Debugging](debug.md), [panics](kernel_panics.md), [security](security.md), [limitations](limitations.md), [future plans](future_plans.md).
- [Kernel modules](lkm.md), [command-line programs](posix_commands.md).

## Verified status versus historical documentation

- The root Makefile builds userspace, `build/kernel/kyroos.elf`, and a Limine ISO. `run` only launches an existing ISO.
- `kyrofs_init` creates a RAM-backed root and imports Limine modules; the shell attempts to launch `/bin/<name>` through the ELF loader.
- A source file, syscall handler, or documented command does **not** establish that it works. ELF/ring-3 execution is currently unstable; full POSIX compliance is not claimed.
- Older filesystem, driver, network, graphics, and security descriptions are pointers for code review, not verified functionality. Check the corresponding `src/kernel/` and `src/include/` implementation and tests before relying on them.

When investigating a triple fault, capture QEMU's first exception (`-d int,cpu_reset -D qemu.log`) and serial output. See the [guide](build_and_run_dev.md).
