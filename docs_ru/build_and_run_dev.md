# Сборка и запуск KyroOS (ветка dev)

Этот гайд описывает текущий Makefile ветки `dev`. Успешная сборка ISO не гарантирует загрузку или работу ELF: userspace и triple fault ещё отлаживаются. Команды выполняются из корня репозитория на Linux.

## Инструменты

Нужны `make`, GCC для x86_64, `ld` (binutils), `nasm`, `xorriso` и `qemu-system-x86_64`. Сборка userspace выполняется отдельным рекурсивным `make`; её настройки находятся в `userspace/Makefile`.

```sh
command -v make gcc ld nasm xorriso qemu-system-x86_64
```

В корневом Makefile зашит `-isystem /usr/lib64/gcc/x86_64-suse-linux/15/include`. На другой версии GCC или дистрибутиве этот путь может потребоваться исправить. Проверь значение `TOOLCHAIN_PREFIX` в начале Makefile и наличие соответствующего компилятора.

В `limine/` есть `BOOTX64.EFI`, `limine-bios.sys`, `limine-bios-cd.bin` и `limine-uefi-cd.bin`, но нет утилиты `limine/limine`. Рецепт ISO подавляет ошибку `bios-install` через `|| true`: созданный ISO сам по себе не доказывает, что BIOS-загрузка работает.

## Сборка

```sh
git clone --branch dev https://github.com/kyrosystems/KyroOS.git
cd KyroOS
make all
```

`all` запускает сборку userspace, затем ядра и ISO. Ядро находится в `build/kernel/kyroos.elf`; ISO создаётся в корне под именем вида `KyroOS-<версия>-<номер_сборки>.iso`. При обычной сборке Makefile изменяет `KYROOS_VERSION_BUILD` в `src/include/version.h`: проверяй `git status` перед коммитом.

`make iso` ожидает уже собранное ядро. `make run` также ничего не собирает: запускает QEMU с существующим ISO. После изменения исходников используй `make all`, затем `make run`.

## Запуск и диагностика

```sh
make run
```

Рецепт QEMU использует `-cdrom`, `-serial stdio`, `-no-reboot`, устройство `e1000` и SDL-дисплей. Для записи исключений укажи реальное имя ISO из корня репозитория:

```sh
ls -lt KyroOS-*.iso
qemu-system-x86_64 -cdrom ./KyroOS-<версия>-<номер_сборки>.iso -serial stdio -no-reboot -d int,cpu_reset -D qemu.log -display none
```

Замени часть имени в угловых скобках на фактическую; `-display none` скрывает framebuffer, поэтому для графики используй `make run`. При triple fault ищи в `qemu.log` первое исключение до double/triple fault и приложи фрагмент вместе с serial-выводом.

## Что попадает в ISO

Рецепт `iso` копирует ядро в `/boot/kernel.elf`, добавляет `module_path` для файлов из `build/isodir/bin/` в `limine.conf` и копирует файлы Limine. `kyrofs_init` импортирует модули загрузчика в файловую систему; shell пытается запускать неизвестные команды как `/bin/<имя>` через ELF-загрузчик. Наличие бинарника в ISO ещё не означает успешный запуск в ring 3.

## Очистка

```sh
make clean
```

Цель удаляет каталоги сборки и вызывает `make -C userspace clean`. Если в корне остался старый ISO, не принимай его за новый: проверь дату изменения и версию в имени.
