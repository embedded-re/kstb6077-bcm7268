// fat.h - FAT32 on a block device (fat.c), shared by the bare-metal programs.
//
// The program provides the block access and a 64 KB work buffer:
//   kstb.h:  #define FAT_BUF <address>       64 KB of RAM for fat.c
//   int fat_blk_read(uint32_t lba, uint32_t n, void *buf);         0 = ok
//   int fat_blk_write(uint32_t lba, uint32_t n, const void *buf);  0 = ok
// LBAs are absolute (512-byte sectors); fat_mount() takes the partition start.
#include <stdint.h>

#define FAT_EIO         (-1)    // block read/write failed
#define FAT_ENOFS       (-2)    // not a FAT32 file system (or not mounted)
#define FAT_ENOENT      (-3)    // no such file or directory
#define FAT_ENOTDIR     (-4)    // a path component is a file
#define FAT_EISDIR      (-5)    // is a directory
#define FAT_EEXIST      (-6)
#define FAT_ENAME       (-7)    // a new name must be 8.3 (up to 8 + 3 characters)
#define FAT_ENOSPC      (-8)
#define FAT_ETOOBIG     (-9)    // file larger than the buffer
#define FAT_ENOTEMPTY   (-10)

struct fat_entry {
    char name[256];             // long name if there is one, else 8.3 ("NAME.EXT")
    char short_name[13];
    uint8_t attr;               // 0x10 = directory
    uint32_t size;
    uint32_t cluster;
    uint16_t date, time;
};

int fat_mount(uint32_t part_lba);
int fat_info(uint32_t *cluster_bytes, uint32_t *clusters, uint32_t *free_clusters);
int fat_list(const char *dir, int (*cb)(const struct fat_entry *e, void *arg), void *arg);
int fat_stat(const char *path, struct fat_entry *e);
int fat_read(const char *path, void *buf, uint32_t max, uint32_t *size);
int fat_write(const char *path, const void *buf, uint32_t size);   // create or replace
int fat_remove(const char *path);                                   // file or empty directory
int fat_mkdir(const char *path);
const char *fat_strerror(int r);

int fat_blk_read(uint32_t lba, uint32_t n, void *buf);
int fat_blk_write(uint32_t lba, uint32_t n, const void *buf);
