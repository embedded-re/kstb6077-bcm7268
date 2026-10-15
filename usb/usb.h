// usb.h - OHCI1 + HID boot keyboard (usb.c), shared by mon/ and doom/.
// The program's kstb.h must define DMA_BASE: 2 MB mapped non-cacheable.
#include <stdint.h>

int usb_init(void);                     // reset, enumerate; 0 = a boot keyboard is polled
int usb_kbd_poll(uint8_t *report);      // 1 = new 8-byte report, 0 = none yet, -1 = not up / error
int usb_hid_char(uint8_t usage, uint8_t mods);  // usage -> character (US layout), 0 if none
void usb_stop(void);
int usb_port_changed(int *connected);  // 1 = plugged/unplugged since the last call
int usb_kbd_up(void);
extern int usb_verbose;                 // 0 = print nothing (default 1)                    // controller back to reset: no more DMA
void usb_status(void);
