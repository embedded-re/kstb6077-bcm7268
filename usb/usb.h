// usb.h - OHCI1, hubs, HID boot keyboard, mass storage (usb.c), shared by mon/ and doom/.
// The program's kstb.h must define DMA_BASE: 2 MB mapped non-cacheable.
#include <stdint.h>

int usb_init(void);                     // reset, enumerate; 0 = a boot keyboard or mouse is polled
int usb_kbd_poll(uint8_t *report);      // 1 = new 8-byte report, 0 = none yet, -1 = not up / error
int usb_hid_char(uint8_t usage, uint8_t mods);  // usage -> character (US layout), 0 if none
void usb_stop(void);
int usb_port_changed(int *connected);  // 1 = plugged/unplugged since the last call
int usb_kbd_up(void);
int usb_mouse_up(void);
int usb_mouse_poll(int *buttons, int *dx, int *dy);   // 1 = new report: buttons bits 0-2, movement
extern int usb_verbose;                 // 0 = print nothing (default 1)                    // controller back to reset: no more DMA
void usb_status(void);
int usb_msc_init(void);                 // the first mass storage device: 0 = ready
int usb_msc_info(uint32_t *blocks, uint32_t *bsize);   // 0 = ready
int usb_msc_read(uint32_t lba, uint32_t n, void *buf);         // 512-byte blocks; 0 = ok
int usb_msc_write(uint32_t lba, uint32_t n, const void *buf);  // 0 = ok
