// mmc.h - eMMC driver (mmc.c).
#include <stdint.h>

// flash0.splash on the modified box (docs/hardware/display.md, "Adding a
// splash partition"): the only area mmc_write() accepts.
#define SPLASH_FIRST    15239168u
#define SPLASH_LAST     15273594u
#define SPLASH_PROTECT  1024u       // first 512 KB: the splash container

#define MMC_EIO      (-1)
#define MMC_ETIMEOUT (-2)
#define MMC_EBUSY    (-3)
#define MMC_ENOCARD  (-4)
#define MMC_EGUARD   (-5)

int mmc_init(void);
int mmc_status(uint32_t rca, uint32_t *status);
int mmc_read(uint32_t lba, uint32_t n, void *buf);
int mmc_read_ext_csd(uint8_t *buf);
int mmc_write_allowed(uint32_t lba, uint32_t n, int force);
int mmc_write(uint32_t lba, uint32_t n, const void *buf, int force);
uint32_t mmc_last_int(void);
const char *mmc_strerror(int r);
