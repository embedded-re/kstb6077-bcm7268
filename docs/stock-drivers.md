# Stock firmware drivers as a guide

The stock box's Android firmware contains Broadcom's own drivers for this
chip. They show how Broadcom powers, clocks and resets each block, with the
real register addresses. This repository uses them as a reference, the same
way it uses BOLT's code (`bolt/bolt.md` §11).

## Getting the files

The files come from an eMMC dump of the stock box, published as the asset
archive of this repository's GitHub release `v1.0`. They are Broadcom and
Kaon binaries, so they stay out of git:

```
tools/fetch-stock          # downloads the release archive, extracts it to stock/ (gitignored)
```

`stock/release/README.txt` lists the contents: boot and recovery images,
`bsu.elf`, Android init scripts, HAL libraries, SAGE TEE apps, IR key maps,
Wi-Fi/BT firmware, kernel modules, and more. The archive also contains
factory DRM/HDCP key blobs (`drm.bin`, `hdcp1xKeys.bin`).

## The useful drivers

| File | What it is | Why it helps |
|---|---|---|
| `stock/release/modules/nexus.ko` | Broadcom **Nexus** kernel driver (ARM32 ELF, kernel 4.1.45), **not stripped**: 24,345 symbols | the chip support library: power/clock tree (`BCHP_PWR_*`), register access (`BREG_*`), reset (`BCHP_Cmn_*`), the GPU driver (`BVC5_*`), display (`BVDC_*`), audio, transport… |
| `stock/release/libs/libnexus.so` | Nexus user-space library | |
| `stock/release/hal/egl/libGLES_nexus.so` | OpenGL ES driver for the V3D GPU | |
| `stock/release/modules/bcmdhd.ko` | Broadcom Wi-Fi driver (`bcmdhd`) | |
| `stock/release/irkeymap/` | IR remote key maps | for the IR remote |

## How to read them

`tools/re/ko.py` works on any unstripped ARM ELF object:

```
K=stock/release/modules/nexus.ko
tools/re/ko.py $K consts 0x21200000 0x21300000   # which functions use which register addresses
tools/re/ko.py $K dis BVC5_P_HardwareResetV3D    # disassembly, literal loads annotated with values
tools/re/ko.py $K pwr GRAPHICS3D                 # a BCHP_PWR resource: type, id, dependencies
tools/re/ko.py $K data BCHP_PWR_P_Resource_HW_V3D  # raw words of a data symbol, relocations resolved
llvm-nm $K | grep BCHP_PWR_P_Resource_           # every power/clock resource by name
tools/re/rdc.py docs/bolt/raw/display/splash0_rdc_lists_0x7db08000.txt 7db08fa0 --names
                                                 # decode RDC display lists (format from BRDC_*)
```

What the code looks like:

- Register accesses are calls to `BREG_Read32(handle, addr)` /
  `BREG_Write32(handle, addr, value)`, with `addr` a **bus address** loaded
  from a literal pool: `0x2xxxxxxx`. The CPU reaches the same register at
  `0xfxxxxxxx` (`hardware/peripherals.md`, "Bus addresses").
- The power tree: each `BCHP_PWR_P_Resource_<NAME>` is `{type, id, name}`;
  `BCHP_PWR_P_Depend_<NAME>` lists the resources it needs first.
  `BCHP_PWR_P_HW_ControlId` switches on the id (a jump table indexed by
  `id & 0xffffff` − 1); each case is a read-modify-write of one clock register.
  Muxes and dividers are in `BCHP_PWR_P_MUX_Control` / `BCHP_PWR_P_DIV_Control`.
- A finding from these drivers is a lead, not a fact, until it is tested on
  the board. The hardware docs only record tested results.
