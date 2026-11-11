# Storage migration completed — 2026-10-26

Linux ROOT was expanded from **5290MiB to 7114MiB (6.95GiB)** by relocating
bare-metal DATA and recreating it as **256MiB FAT32**. Linux gained **1824MiB**.
After the final boot and regression checks, `df` reports **3.37GiB available,
48% used**, compared with about 1.8GB available and 65% used before migration.
The installed applications, profiles and project environments were retained.

## Resulting layout

Sectors are 512 bytes. Other partition boundaries are unchanged.

| Partition | First LBA | Last LBA | Capacity |
|---|---:|---:|---:|
| p8 ROOT, ext4 | 145408 | 14714879 | 7114MiB |
| p9 DATA, FAT32 | 14714880 | 15239167 | 256MiB |
| p10 splash, protected | 15239168 | 15273594 | 16.81MiB |

The GPT retains **14 entries**, disk/partition GUIDs, types and names, and the
fixed splash location. A generic 128-entry GPT would overlap the protected
splash tail. ROOT's UUID remains `435cda17-99fa-492f-ba47-d04cc34286ea`;
its ext4 filesystem now has **1,821,184 blocks of 4096 bytes**. DATA uses
2KiB clusters, 130,554 clusters, label `KSTB`, and its updated hidden-sector
field is 14714880. Its original eight files total 26,660,796 bytes.

## Execution and preservation

The [inspection and alternatives](appendix-storage-repartition-plan.md) preceded user
authorization of the recommended layout. A RAM rescue boot using the installed
kernel #30 was verified with working eMMC, network, SSH and filesystem tools.
ROOT and DATA remained unmounted during physical migration. A live userspace
watchdog guarded that operation; its device descriptor was checked directly
because this kernel does not expose watchdog status through sysfs.

Complete offline raw images of old ROOT (5,546,967,040 bytes) and old DATA
(2,181,038,080 bytes) were streamed to the PC, gzip-checked, decompressed and
SHA256-matched before destructive writes. GPT and protected regions were also
captured. Recovery artifacts remain in the PC's private, gitignored
`port/build/storage-migrate/20261026/recovery/` directory; they contain private
system and factory data and must not be published.

The new DATA image was written and read back with an exact hash match before
changing the six reviewed GPT sectors. The kernel reread the table; GPT/FAT
checks passed. ROOT passed offline `e2fsck` before growth, `resize2fs` grew it,
and a second offline `e2fsck` passed. Identical offline manifests covered
**58,481 entries**, including regular-file contents, links, ownership and
permissions. Original DATA file hashes also matched. Temporary rescue files
were subsequently removed and FAT checked again.

Final SHA256 checks confirm that factory sectors 34–6143, KRN, DTB, splash,
boot0 and boot1 are unchanged. Neither NVRAM nor golden/slot A was rewritten.
Kernel **6.18.57-kstb-hrtimer-ab #30** and the installed native **Mesa r34**
remain in place; `/usr/lib/dri/v3d_dri.so` has its original SHA256
`ab8e3f40c4d368bc46661342bde624de3051a249aeed1b02f220e39b6e8bcb8a`.

## Verified milestone

- Two normal boots followed BOLT → DATA kboot at EL3 → kmon → normal Linux;
  kmon reported monitor `4b423303`, and Linux returned with all four CPUs.
- On the first normal boot, kmon read `kboot.bin`, wrote/read a 21,704-byte
  duplicate with matching CRC `a3fce739`, then deleted it. The second boot
  succeeded after removal of all four temporary rescue files.
- The existing four-thread CPU classifier self-test passed (Samoyed index 258,
  approximately 103ms inference in this single regression sample).
- 100 native GPU fence export/import/server-wait chains passed with exact pixels.
- Native 4× MSAA, depth and stencil checks passed with zero pixel failures;
  GLES capability enumeration completed. The guard found no GPU reset/MMU fault.
  Existing `drm_sched_entity_init` scheduler warnings remain recorded.

The desktop still starts manually. No desktop session or long-running benchmark
was left running. This verifies the storage migration and a short application
regression gate; it is not a fresh full GPU conformance or endurance campaign.
[Execution logs and offline evidence checks](evidence/storage/20261026-migration/README.md).

## Follow-up bare-metal and Linux regression

The user-requested [broader platform check](evidence/storage/20261026-platform-check/README.md)
passed after this milestone: monitor RAM/storage/network/interrupts and USB,
bare-metal Doom launch and clean return, Linux persistent I/O, CPU math,
repeated classifier/RAM check, actual GPU FFT, GPU desktop restart/launcher/
fullscreen transitions, networking and native-format normal-user HDMI playback.
The box was left in Linux with no desktop or test workload running. Physical
sound/input and unexercised hardware remain outside that automated check.

## Recovery constraints

Full-image backups were verified, but a complete rollback was **not** executed.
After filesystem growth, **restoring only the old GPT would truncate ROOT**.
Rollback requires booting RAM rescue with ROOT/DATA unmounted, restoring the
verified old ROOT image first, restoring the old DATA image at its original
whole-device offset (LBA 10979328), then restoring reviewed old GPT metadata
and rechecking filesystems and protected regions before normal boot.

The `backup/gptbak.bin` file preserved inside DATA is a historical backup,
not the current migration rollback table. Current rollback materials are on
the PC. Retain these until the new layout has had an agreed retention period.
