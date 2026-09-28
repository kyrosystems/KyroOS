#include "lkm.h"
#include "log.h"

int module_init(void) {
    klog(LOG_INFO, "Hello from the example kernel module!");
    return 0;
}

void module_exit(void) {
    klog(LOG_INFO, "Goodbye from the example kernel module!");
}

LKM_INIT(module_init);
LKM_EXIT(module_exit);