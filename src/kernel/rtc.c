#include "rtc.h"
#include "port_io.h"
#include "log.h"
#include "kstring.h"

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

static int rtc_updating() {
    outb(CMOS_ADDRESS, 0x0A);
    return (inb(CMOS_DATA) & 0x80);
}

static uint8_t rtc_read_register(uint8_t reg) {
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

void rtc_init() {
    // Basic RTC init if needed, usually BIOS handles it.
}

rtc_time_t rtc_get_time() {
    rtc_time_t time;
    
    while (rtc_updating()); // Wait for update to finish

    time.second = rtc_read_register(0x00);
    time.minute = rtc_read_register(0x02);
    time.hour   = rtc_read_register(0x04);
    time.day    = rtc_read_register(0x07);
    time.month  = rtc_read_register(0x08);
    time.year   = rtc_read_register(0x09);

    uint8_t registerB = rtc_read_register(0x0B);

    // Convert BCD to binary values if necessary
    if (!(registerB & 0x04)) {
        time.second = (time.second & 0x0F) + ((time.second / 16) * 10);
        time.minute = (time.minute & 0x0F) + ((time.minute / 16) * 10);
        time.hour   = ( (time.hour & 0x0F) + (((time.hour & 0x70) / 16) * 10) ) | (time.hour & 0x80);
        time.day    = (time.day & 0x0F) + ((time.day / 16) * 10);
        time.month  = (time.month & 0x0F) + ((time.month / 16) * 10);
        time.year   = (time.year & 0x0F) + ((time.year / 16) * 10);
    }

    // Convert 12 hour clock to 24 hour clock if necessary
    if (!(registerB & 0x02) && (time.hour & 0x80)) {
        time.hour = ((time.hour & 0x7F) + 12) % 24;
    }

    // Calculate full year
    // Heuristic: if year < 80, assume 20xx, else 19xx. 
    // Or read century register (usually 0x32, but varies).
    // Let's assume 2000s for simplicity in this OS context.
    time.year += 2000;

    return time;
}

void rtc_print_time() {
    rtc_time_t t = rtc_get_time();
    char buf[64];
    ksprintf(buf, "%02d:%02d:%02d %02d/%02d/%d", t.hour, t.minute, t.second, t.day, t.month, t.year);
    klog_print_str(buf, true);
}
