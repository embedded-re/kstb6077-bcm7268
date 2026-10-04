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
| From bare metal | no eMMC driver yet; BOLT copies data into RAM before `go` (~15 MB/s) |
| Data area | `flash0.splash` from offset 1 MB, with a `KWAD` header (`tools/make-wad-image`) |
| Tested | SDHCI register reads, writing and loading a 14.4 MB file through BOLT |

## The two SDHCI controllers (tested)

Read on the stock box on 2026-10-04 at `BOLT>` after a power-on
(`../evidence/sdhci_genet/`). Each controller has a standard SD host block
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

## Reading the eMMC from bare metal

A bare-metal program can't read the eMMC itself (no driver yet), but BOLT can
copy any part of a partition into RAM before `go`. The modified box's
`flash0.splash` partition is 16.8 MB, and BOLT's splash uses only the first
512 KB of it. The rest can hold a large data file: 4 MB loads in about a
quarter of a second. Over serial the same 4 MB takes ~50 minutes.

All of this was tested on the modified box on 2026-10-02: first with a
random 4,196,020-byte test file, then with a 14.4 MB file (below), which is
what the partition holds now.

## Layout of `flash0.splash` (modified box)

`flash0.splash` = LBA 15239168–15273594 = 34,427 sectors = 17,626,624 bytes
(`display.md`, "Adding a splash partition").

| Partition offset | Contents |
|---|---|
| `0x000000–0x07ffff` | splash container (`GZBR`…). BOLT reads exactly these 512 KB at boot. The current container uses `0x2f2e` bytes, and the first 12 KB have CRC `0x985d62dc` |
| `0x080000–0x0fffff` | unused (zeros) |
| `0x100000–0xec6fff` | **data image**: 512-byte `KWAD` header + file. Room: 16,578,048 bytes. Now: a 14.4 MB file (below), image `0xdc7000` bytes |

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

**Write once** (from the BOLT prompt, file over TFTP; see `../bolt/bolt.md` §9):

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
  (`display.md`), so stay in partition devices.
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

Free for a program's data: `0x10000000…` (BOLT `rmem`, `memory-map.md`).
Avoid BOLT `0x06ffc000–0x09200000`, PSCI `0x06400000`, `splash0`
`0x7db08000–0x7df00000`, the second framebuffer `0x7d600000–0x7d9f47ff`,
and BOLT's flash staging buffer `0x00040000+` while running `flash`.

## Rollback

The data image doesn't touch the first 512 KB, so the splash keeps working.
To remove it, overwrite `0x100000…` with zeros the same way. Before any
write, check the container: `load -raw -rawfs -addr=0x02000000 -max=0x3000
flash0.splash`, then `crc -offset=0x02000000 -size=0x3000` = `0x985d62dc`
(it was the same before and after this write). If it is ever damaged,
rebuild it with `tools/make-splash`.
