# Storage: data files on the eMMC for bare-metal programs

The box has one storage device: the eMMC on SDHCI 1 (`0xf0200200`; Linux
`mmc1`, card name `DG4008`, 7.28 GiB, "high speed MMC card"). There is
**no SD card slot** on the case, although SDHCI 0 (`0xf0200000`) is enabled
in the DTB and Linux registers it as `mmc0`.

## Summary

| | |
|---|---|
| eMMC | 7.28 GiB on SDHCI 1 (`0xf0200200`), 8-bit, 50 MHz |
| SDHCI 0 | `0xf0200000`, removable-slot type, no card, no slot on the case |
| From bare metal | **own driver** in kmon ([`../../mon/mmc.c`](../../mon/mmc.c)): read ~33 MB/s, write ~9 MB/s, PIO; or BOLT copies data into RAM before `go` (~15 MB/s) |
| Data area | `flash0.splash` from offset 1 MB, with a `KWAD` header ([`tools/make-wad-image`](../../tools/make-wad-image)) |
| Tested | SDHCI register reads, writing and loading a 14.4 MB file through BOLT; reading and writing from bare metal (kmon, 2026-10-15) |

## The two SDHCI controllers (tested)

Read on the stock box on 2026-10-04 at `BOLT>` after a power-on
([`../evidence/sdhci_genet/`](../evidence/sdhci_genet/)). Each controller has a standard SD host block
and a Broadcom config block (DTB `reg-names` "host", "cfg"). Register names
are from the SD Host Controller spec; the values are tested. The buffer data
port `+0x20` was not read (reading it pops the FIFO).

| | SDHCI 0 | SDHCI 1 (eMMC) |
|---|---|---|
| host / cfg | `0xf0200000` / `0xf0200100` | `0xf0200200` / `0xf0200300` |
| `+0xfc` version | `10020000`: spec **3.00**, vendor `0x10` | same |
| `+0x40` capabilities | `05ea6432` | `45ee6432` |
| slot type (cap bits 31:30) | **removable** | **embedded** |
| 8-bit bus (cap bit 18) | no | yes |
| base clock (cap bits 15:8) | 100 MHz | 100 MHz |
| also in both | ADMA2, SDMA, high speed, 3.3 V and 1.8 V, max block 2048 | |
| `+0x44` capabilities 2 | `0000a575` | `0000a525` |
| `+0x24` present state | `01fa0000`: **no card** (bit 16 clear), card-detect level 0 | `1fff0000`: card present, CMD and DAT0–7 high, idle |
| `+0x28` host control / power | `00800000`: power off | `00000f24`: **8-bit**, high speed, SDMA; power on at 3.3 V |
| `+0x2c` clock | `0`: clock off | `000e0107`: clock on, divider 1 = base / 2 (50 MHz) |

The eMMC controller still holds BOLT's last transfer: command `+0x0e` =
`0x123a` (**CMD18**, read multiple blocks), transfer mode `0x37` (SDMA,
multi-block, auto CMD12, read), block size 512 (`+0x04` = `00007200`),
argument `0x9a00`, SDMA address `0x076a4018` (a buffer in BOLT's RAM), and
response `0x900` (card status: ready for data, state "tran").

SDHCI 0 is wired as a removable-card slot but no card is detected, and the
case has no slot. Whether the board has an unpopulated slot footprint is not
checked.

The cfg blocks read alike (`+0x00` = `40003c03`, `+0xe4` = `03001170`) and
mirror some host values (e.g. `+0x18` = the host's `+0x60` preset
`00020080`). SDHCI 0's cfg block aborts at `+0x12c`, `+0x15c–0x1e3` and
`+0x1e8–0x1fb`; SDHCI 1's answers everywhere. `0xf0200400` answers too
(`+0x04` = `40`, `+0x08` = `00010100`, `+0x0c` and `+0x30–0x3b` abort). The
cfg registers' names are unknown.

## The eMMC driver (tested)

Tested on the modified box on 2026-10-15 with kmon's `mmc` command
([`../../mon/README.md`](../../mon/README.md)), at EL2 after `go -64`.

**BOLT's set-up survives `go -64`**, so the driver doesn't initialise the
card. After `go` the host registers are the same as at `BOLT>` (present
`1fff0000`, control `00000f24` = 8-bit + high speed, clock `000e0107` =
50 MHz), and the last command is BOLT's own `load` of the kmon bundle: CMD18,
argument `0x00e90dc0`, SDMA address `0x002c0000`, the end of that load. That
also confirms **sector addressing: LBA = 15239168 + offset / 512** inside
`flash0.splash`.

| | |
|---|---|
| Card address (RCA) | **2** (CMD13 to RCA 1 times out; RCA 2 answers `00000900` = state tran, ready for data) |
| Commands used | CMD8 (EXT_CSD), CMD13, CMD17/18 read, CMD24/25 write, auto-CMD12; programmed I/O through `+0x20` |
| Interrupt status enable `+0x34` | BOLT leaves `13ff060f`: buffer read/write ready (bits 5/4) are off, the driver turns them on. Signal enable `+0x38` stays `0` (no GIC interrupt) |
| Read | 14,446,592 bytes (the WAD image, 28,216 sectors) in 428 ms = **33 MB/s**; CRC `0x81c4660f` = the stored image. 25 sectors (the splash container): CRC `0x3fffa0ed`, matches |
| Write | 128 sectors (64 KB) of random data with CMD25 in 6 ms; read back, CRC matched the file. One sector with CMD24: exactly that sector changed. Then zeros written back and checked |
| Write area used for the test | `flash0.splash +0x80000` (LBA 15240192–15240319), unused; zeros again afterwards (CRC `d7978eeb`) |

EXT_CSD, read from the card with CMD8:

| Field | Value | Meaning |
|---|---|---|
| EXT_CSD_REV [192] | 8 | eMMC 5.1 |
| SEC_COUNT [215:212] | 15,273,600 | 7,457 MiB user area; the last LBA is 15,273,599 |
| PARTITION_CONFIG [179] | `48` | boot acknowledge on, boot from boot partition 1, access = user area |
| BOOT_SIZE_MULT [226] | 32 | two 4 MB boot partitions (BOLT's) |
| RPMB_SIZE_MULT [168] | 32 | 4 MB RPMB |
| HS_TIMING [185] | 1 | high speed |
| BUS_WIDTH [183] | 0 | although the bus runs 8-bit; probably not readable back (inferred) |
| DEVICE_TYPE [196] | `57` | |
| BOOT_WP [173] / status [174] | `00` / `00` | boot partitions not write-protected |
| PARTITIONING_SUPPORT [160] / SETTING_COMPLETED [155] | `07` / `00` | no enhanced/GP partitions set up |
| Life time estimate A/B [268/269] | `01` / `01` | 0–10 % of the rated wear used |
| PRE_EOL_INFO [267] | `01` | normal |
| FIRMWARE_VERSION [261:254] | `37 33 31 30 33 35 31 37` | ASCII "73103517" |

GPT read with the driver (`mmc part`, entries at LBA 2). Since 2026-10-15
the Linux port's `SWAP` and `ROOT` are replaced by a 1 GB `ROOT` and the
FAT32 `DATA` partition ("The `DATA` partition" below):

| # | First LBA | Last LBA | Name |
|---|---|---|---|
| 1 | 34 | 34 | macadr |
| 2 | 35 | 162 | nvram |
| 3 | 163 | 2046 | bsu |
| 4 | 2048 | 4095 | misc |
| 5 | 4096 | 6143 | hwcfg |
| 6 | 6144 | 137215 | KRN |
| 7 | 137216 | 145407 | DTB |
| 8 | 145408 | 2242559 | ROOT (1 GB, kept for a minimal Linux rootfs; empty) |
| 9 | 2242560 | 15239167 | **DATA** (6346 MB, FAT32) |
| 10 | 15239168 | 15273594 | splash |

Safety rules in the driver: it never sends CMD6 (SWITCH, which could change
the boot or partition configuration), erase or lock commands, and
`mmc_write()` refuses anything outside `flash0.splash` and `flash0.DATA`,
and the splash container (its first 512 KB) unless forced (tested: LBA 100
in `nvram` and the container's first sector were both refused).

## The `DATA` partition (modified box)

Made on 2026-10-15 from the Linux port's `SWAP` (2048 MB) and `ROOT`
(5322 MB). `KRN`, `DTB` and everything before them, and `splash`, are
unchanged.

| Entry | Before | After |
|---|---|---|
| 8 | `SWAP` LBA 145408–4339711, type Linux swap | `ROOT` LBA 145408–**2242559** (1 GB), type Linux filesystem |
| 9 | `ROOT` LBA 4339712–15239167, type Linux filesystem | **`DATA`** LBA **2242560**–15239167 (6346 MB), type Microsoft basic data, new unique GUID |

Written the same way as the splash change ([`display.md`](display.md), "Adding a splash
partition"): sectors 0–5 built on the PC, checked with `sfdisk --verify` on
a sparse image ("No errors detected"; only the missing backup GPT reported),
uploaded at `BOLT>` with `tools/kstb-run --no-go --addr 0x02200000`, written
with `flash -noerase -mem=0x02200000 -memsize=0xc00 mem0 flash0`, read back
(CRC `b10c9741`, same as the file). Only sectors 1, 3 and 4 changed. After a
reset BOLT lists `flash0.ROOT` (1024 MB) and `flash0.DATA` (6346 MB), and the
autostart from `flash0.splash` works as before. Images:
[`../evidence/gpt/modified-box_primary-gpt_lba0-5_data.bin`](../evidence/gpt/modified-box_primary-gpt_lba0-5_data.bin); writing back
`…_with-splash.bin` the same way restores the previous table. The backup GPT
at the end of the eMMC still describes the original layout (as before).

**FAT32**: `mkfs.fat -F 32 -s 64 -S 512 -n KSTB -h 2242560` on a sparse
6346 MB image on the PC: 32 KB clusters, 64 reserved sectors, two FATs of
1600 sectors, root directory in cluster 2, 203,020 clusters. The first 3328
sectors (1.6 MB: reserved area, both FATs, the root cluster) were written
from kmon with zeros everywhere `mkfs` left zeros, so no old swap data
remains in them. Read back: CRC `aef8579e`, the same as the image, which
`fsck.fat` reports clean (empty).

**Files from bare metal** (tested 2026-10-15): kmon's FAT32 driver
([`../../fat/`](../../fat/), commands in [`../../mon/README.md`](../../mon/README.md)) on the eMMC driver.

| Test | Result |
|---|---|
| 64 KB file written, read back | identical (CRC `713f7468`), 26 ms / 5 ms |
| the 14.4 MB WAD (441 clusters) | written in 1152 ms (~12.5 MB/s), read in 435 ms (~33 MB/s), identical |
| a directory, a 1000-byte file in it, replacing a file with a shorter one | read back identical |
| delete a file, refuse a non-empty directory, delete the empty one | as expected |
| PC check: the changed sectors (boot, FSInfo, both FATs, the directories) dumped with `mmc dump` into the `mkfs` image | `fsck.fat`: no errors, only "Free cluster summary uninitialized" (kmon sets FSInfo's free count to unknown on purpose); `mdir` lists the files with the right sizes |

New names are 8.3 only (no long-name records are written); lowercase names
get the NT lowercase flags, so PCs show them as typed. Dates are fixed at
2026-01-01 (no clock). `flash0.DATA` now holds `doom1.wad`.

## Boot partitions and backups (modified box, tested 2026-10-15)

BOLT read both eMMC boot partitions into RAM (`load -raw -rawfs
-max=0x400000 flash1` / `flash2`). kmon's driver can't read them itself: it
never sends CMD6, which switching to a boot partition needs.

| | Contents | CRC32 (4 MB) |
|---|---|---|
| boot0 (`flash1`) | BOLT: ARM32 vector table at offset 0, data up to `0x145f34` (1.27 MB), zeros after that | `2778e541` |
| boot1 (`flash2`) | **all zeros**: every 32-bit word is 0 | `1147406a` |

PARTITION_CONFIG `48` boots from boot partition 1, so **there is one copy of
BOLT on the box and no fallback**.

Backups, each checked by CRC against kmon's `crc` of the same RAM:

| File | What | Size | CRC32 |
|---|---|---|---|
| `boot0.bin` | boot partition 1 (BOLT) | 4 MB | `2778e541` |
| `boot1.bin` | boot partition 2 (zeros) | 4 MB | `1147406a` |
| `lba0.bin` | user area LBA 0–6143: protective MBR, primary GPT, `macadr`, `nvram`, `bsu`, `misc`, `hwcfg` | 3 MB | `d8ef1f15` |
| `gptbak.bin` | LBA 15273567–15273599: backup GPT entries and header (stale on purpose, see [`display.md`](display.md)) | 16,896 B | `8afde2c0` |

- All four are on the box in `flash0.DATA` under `backup/` (kmon `fput`)
  and on the PC, sent with BOLT `save` over TFTP ([`../booting.md`](../booting.md)).
- `boot0.bin` and `boot1.bin` are also in the GitHub release `v1.0` as
  `KSTB6077_bolt_boot_backup.zip`, with CRC32 and SHA-256 in its
  `README.txt`:
  `gh release download v1.0 -R embedded-re/kstb6077-bcm7268 -p KSTB6077_bolt_boot_backup.zip`.
  The other release asset, `KSTB6077_assets.zip` (`tools/fetch-stock`), has
  no BOLT and no raw factory partitions.
- Sectors 0–5 of `lba0.bin` are the same as
  [`../evidence/gpt/modified-box_primary-gpt_lba0-5_data.bin`](../evidence/gpt/modified-box_primary-gpt_lba0-5_data.bin).

## Reading the eMMC with BOLT before `go`

Without a driver, BOLT can
copy any part of a partition into RAM before `go`. The modified box's
`flash0.splash` partition is 16.8 MB, and BOLT's splash uses only the first
512 KB of it. The rest can hold a large data file: 4 MB loads in about a
quarter of a second. Over serial the same 4 MB takes ~50 minutes.

All of this was tested on the modified box on 2026-10-02: first with a
random 4,196,020-byte test file, then with a 14.4 MB file (below), which is
what the partition holds now.

## Layout of `flash0.splash` (modified box)

`flash0.splash` = LBA 15239168–15273594 = 34,427 sectors = 17,626,624 bytes
([`display.md`](display.md), "Adding a splash partition").

| Partition offset | Contents |
|---|---|
| `0x000000–0x07ffff` | splash container (`GZBR`…). BOLT reads exactly these 512 KB at boot. Since 2026-10-15 the container has a silent `pcm0` for the Doom port's sound ([`../../doom/README.md`](../../doom/README.md)): `0x30c4` bytes, CRC `0x3fffa0ed` (checked on read-back). Before that: `0x2f2e` bytes, first 12 KB CRC `0x985d62dc` |
| `0x080000–0x0fffff` | unused (zeros) |
| `0x100000–0xec6fff` | **data image**: 512-byte `KWAD` header + file. Room: up to the kmon bundle at `0xf00000` (14,680,064 bytes). Now: a 14.4 MB file (below), image `0xdc7000` bytes |
| `0xec7000–0xefffff` | unused |
| `0xf00000–0xfccfff` | **kmon bundle** (since 2026-10-15): the monitor padded to 256 KB, then `doom.bin` in a `KWAD` header; `0xcd000` bytes, written and read back by `tools/kstb-mon --install` (CRC matched), and since then also from kmon itself with `mmc write` (CRC matched). Loaded to `0x00100000` ([`../../mon/README.md`](../../mon/README.md)) |
| `0xfcd000–0x10cf5ff` | unused |

**Nothing else reads past 512 KB (tested: from BOLT's code).** The container
reader `0x07011304` opens `flash0.splash` once, allocates `0x80000` bytes,
reads exactly `0x80000` (anything else → `SPLASH: file read failed`), and
closes it. The only other code that names `flash0.splash` (`0x070107e0`)
prints `SPLASH:  %u; %s` with 524288 and the name. The "splash-feedback"
media routines (`0x07010994`, `0x07025358`) look items up in the payload
already in RAM (`0x07011430`). After writing the data image and
rebooting, the boot log still shows `Loaded BMP: W=1920 H=1080`.

## What is stored now

A game data file, `doom1.wad` (14,445,628 bytes, md5
`88ce96442d269ef515b39fe34f08a9b7`), stored as a `KWAD` image:

| | Value |
|---|---|
| `KWAD` image | `0xdc7000` bytes, CRC `0x81c4660f` |
| file in it | `0xdc6c3c` bytes at `+0x200`, CRC `0x0874d1c3` |
| `flash` | 603 ms |
| `load` | 979 ms (~14.8 MB/s) |
| in RAM after `load -addr=0x10000000` | `0x10000000–0x10dc6fff` |

Read back after zeroing the RAM (CRC `0x3a2a6a0f` before the load): image
`0x81c4660f`, file `0x0874d1c3`, header `KWAD`, name `doom1.wad`. The splash
container is unchanged (`0x985d62dc`).

## The `KWAD` image header

Built by `tools/make-wad-image file img`. One sector, then the file, padded
with zeros to a whole sector:

```
+0x00  'KWAD'          magic                       (4b 57 41 44)
+0x04  u32  1          version
+0x08  u32  size       file size in bytes
+0x0c  u32  crc32      standard zlib crc32 of the file (same as BOLT `crc`)
+0x10  u32  0x200      offset of the file from the header
+0x14  char name[32]   file name, NUL-padded
+0x200 the file
```

The program checks the magic and size before using the data. The CRC lets
it (or BOLT's `crc -offset=<addr+0x200> -size=<size>`) confirm that the right
file was loaded.

## Recipe

**Write once** (from the BOLT prompt, file over TFTP; see [`../bolt/bolt.md`](../bolt/bolt.md) §9):

```
ifconfig eth0 -auto
load -tftp -raw -addr=0x10000000 -max=0x500000 192.168.1.38:wadtest.img
crc -offset=0x10000000 -size=0x400a00             ← must equal the PC's image CRC
flash -noerase -offset=0x100000 -mem=0x10000000 -memsize=0x400a00 mem0 flash0.splash
```

(These are the test file's numbers; for the file now stored, use
`-memsize`/`-max` `0xdc7000`.) Measured: TFTP 4 MB in a few seconds; `flash` 217 ms
(`Programming...done. 4196864 bytes written`); CRC `0x37b79960` on the PC,
after TFTP, and after read-back.

**Load before each `go`:**

```
load -raw -rawfs -offset=0x100000 -addr=0x10000000 -max=0x400a00 flash0.splash
```

Measured: 4,196,864 bytes in 267–280 ms (~15 MB/s). Tested after a
software reset, with the target RAM zeroed first (`f -b 0x10000000 0x400a00 0`,
CRC then `0x55de8ba8`); after the load the CRC was `0x37b79960` again and
`d -w 0x10000000` showed the `KWAD` header.

Not yet tested: that the loaded data is intact inside a `go -64` program
(inferred yes: `go` doesn't clear RAM, and probes already use `0x10000000+`).

## `flash` and `load` options (from BOLT's code and `help`)

The `flash` command is at `0x0702fa6c`:

- **`-offset=N` is the destination offset** in the target device. The final
  call is `write(dev, buf, N, len)`, after a check that `N + len` fits in the
  device (`ERROR: File larger than flash device…`). `-offset` is parsed into
  a 64-bit value, but large raw-`flash0` offsets were seen to fail or wrap
  ([`display.md`](display.md)), so stay in partition devices.
- **`-noerase`** skips the erase step. Without it, eMMC targets get an erase
  of `[N, N + len)` first.
- **From `mem0`, BOLT first copies the data to its staging buffer at
  `0x00040000`** (16 MB), and writes from there. Keep the source
  (`-mem=`) outside `0x00040000 … 0x00040000 + memsize`, or the copy
  overlaps itself.
- An image of exactly 16 MB (`0x1000000`) is refused unless `-forcewrite` is
  given (the check is for a file that filled the staging buffer).

`load -offset=N` is the **source** offset in the file or device ("Begin
loading at this offset in the file or device", `help load`; the read-back
above starts at the header, not at the splash container).

## RAM to use

Free for a program's data: `0x10000000…` (BOLT `rmem`, [`memory-map.md`](memory-map.md)).
Avoid BOLT `0x06ffc000–0x09200000`, PSCI `0x06400000`, `splash0`
`0x7db08000–0x7df00000`, the second framebuffer `0x7d600000–0x7d9f47ff`,
and BOLT's flash staging buffer `0x00040000+` while running `flash`.

## Rollback

The data image doesn't touch the first 512 KB, so the splash keeps working.
To remove it, overwrite `0x100000…` with zeros the same way. Before any
write, check the container: `load -raw -rawfs -addr=0x02000000 -max=0x30c4
flash0.splash`, then `crc -offset=0x02000000 -size=0x30c4` = `0x3fffa0ed`
(the container with `pcm0`, since 2026-10-15; the WAD write was checked the
same way against the older container, `-size=0x3000` = `0x985d62dc`). If it is ever damaged,
rebuild it with [`tools/make-splash`](../../tools/make-splash).
