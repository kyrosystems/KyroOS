# Документация KyroOS (dev)

Здесь описана кодовая база ветки `dev`. Начинай с [актуального гайда сборки и запуска](build_and_run_dev.md). Старый [build.md](build.md) создан до перехода на нынешний Makefile и может содержать устаревшие команды и имена артефактов.

## По подсистемам

- [Обзор](overview.md), [ядро](kernel.md), [загрузка](boot.md), [память](memory.md), [прерывания](interrupts.md).
- [Userspace](userspace.md), [системные вызовы](syscalls.md), [написание приложений](writing_applications.md), [портирование](porting_applications.md).
- [Файловая система](filesystem.md), [IPC](ipc.md), [драйверы](drivers.md), [сеть](networking.md).
- [Графика](graphics.md), [графические приложения](graphics_applications.md).
- [Отладка](debug.md), [паники](kernel_panics.md), [безопасность](security.md), [ограничения](limitations.md), [планы](future_plans.md).

## Что подтверждено кодом, а что нет

- Корневой `Makefile` собирает userspace, ядро `build/kernel/kyroos.elf` и ISO с Limine. `run` только запускает уже созданный ISO.
- `kyrofs_init` создаёт RAM-корень и добавляет модули Limine; shell пытается запускать `/bin/<имя>` через ELF-загрузчик.
- Присутствие исходника утилиты, обработчика syscall или пункта в документации **не означает**, что функция работает. Запуск ELF/ring 3 в текущей ветке нестабилен; полной POSIX-совместимости не заявляем.
- Описание дисковой ФС, драйверов, сети, графики и защиты в старых разделах — ориентир для чтения кода, а не гарантия работоспособности. Перед использованием сверяй с соответствующим `src/kernel/`, `src/include/` и тестами.

Для воспроизведения triple fault записывай первое исключение QEMU (`-d int,cpu_reset -D qemu.log`) и serial-вывод. См. [гайд](build_and_run_dev.md).
