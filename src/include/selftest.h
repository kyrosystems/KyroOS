#ifndef SELFTEST_H
#define SELFTEST_H

// Boot-time integration self-tests (see src/kernel/selftest.c).
// Enable with: tests/run_qemu_tests.py, or KSELFTEST=1 env in kernel.c
void selftest_init(void);

#endif // SELFTEST_H