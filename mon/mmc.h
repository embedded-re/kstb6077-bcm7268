// mmc.h - eMMC driver (mmc.c).
#include <stdint.h>

// The only areas mmc_write() accepts, on the modified box:
// flash0.splash (docs/hardware/display.md, "Adding a splash partition") and
// flash0.DATA, the FAT32 partition for files (docs/hardware/storage.md).
// splash is fixed (kboot reads it at a fixed LBA); DATA is looked up in the
// GPT by name (mmc_data_range), so the partitions before it can change.
#define SPLASH_FIRST    15239168u
#define SPLASH_LAST     15273594u
#define SPLASH_PROTECT  1024u       // first 512 KB: the splash container
#define DATA_MIN_LBA    6144u       // DATA must start after hwcfg (LBA 4096-6143)

#define MMC_EIO      (-1)
#define MMC_ETIMEOUT (-2)
#define MMC_EBUSY    (-3)
#define MMC_ENOCARD  (-4)
#define MMC_EGUARD   (-5)
#define MMC_ENOPART  (-6)

int mmc_init(void);
int mmc_reset_card(uint32_t cid[4]);
int mmc_status(uint32_t rca, uint32_t *status);
int mmc_read(uint32_t lba, uint32_t n, void *buf);
int mmc_read_ext_csd(uint8_t *buf);
int mmc_find_part(const char *name, uint32_t *first, uint32_t *last);
int mmc_data_range(uint32_t *first, uint32_t *last);
int mmc_write_allowed(uint32_t lba, uint32_t n, int force);
int mmc_write(uint32_t lba, uint32_t n, const void *buf, int force);
uint32_t mmc_last_int(void);
const char *mmc_strerror(int r);
