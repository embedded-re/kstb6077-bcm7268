# Bare-metal and Linux regression after repartitioning — 2026-10-26

**Both completed successfully.** The box is back in normal Linux with all four
CPUs, kernel #30, native Mesa r34 and about 3.37GiB free (48% used). No desktop,
benchmark or userspace watchdog was left running. This follows the
[storage migration](../../../05-storage-repartition.md).

## Bare metal

| Check | Result |
|---|---|
| Normal BOLT → DATA kboot → kmon boot, EL3/PSCI monitor | PASS, identifier `4b423303` |
| GPT / relocated DATA | PASS, LBA 14714880–15239167, FAT32 2KiB clusters |
| Original eight DATA files | PASS, each read through kmon and compared by CRC; final Linux SHA256 checks also match |
| RAM fill, copy and CRC | PASS, 1MiB; `cmp` reports `same` |
| FAT write/read/delete | PASS, temporary 1MiB pattern, CRC matched, file removed |
| GIC / EL2 timer | PASS, 10ms interrupts counted |
| Ethernet | PASS, DHCP and three replies from development PC |
| HDMI | Display running, built-in script CRC good, connected indication |
| Audio | Init, 200ms tone and stop commands completed; audibility not remotely confirmed |
| USB | Keyboard `0461:0010` enumerated, boot-protocol endpoint ready |
| Installed Linux images | KRN/DTB headers readable; normal Linux boot succeeded |
| Bundled bare-metal Doom | WAD CRC good, graphics/sound initialized, menu Quit returned **status 0** to kmon; normal Linux boot then succeeded |

The first Doom automation attempt omitted Enter after selecting Quit. It did
not exit, and the armed 90-second watchdog recovered to the normal monitor.
Source inspection showed that menu letters select an item; Enter activates it.
The corrected sequence **Escape, q, Enter, y** returned cleanly. No bare-metal
binary or firmware was changed. The first attempt and corrected result are
both retained; the failed attempt does not establish a game hang.

## Linux

| Check | Result |
|---|---|
| ROOT I/O | PASS, 16MiB write/fsync/read/hash/delete |
| ROOT persistence | PASS, separate 1MiB fsynced pattern survived reboots, verified and removed |
| GPT and FAT filesystem checks | PASS; exact eight-file DATA inventory, no extra files |
| CPU / numerical types | PASS, NEON FP32/FP64 FMA, binary128 software, integer128, atomic64, FP64 FFT and Cholesky solve |
| Python / NumPy | PASS, 210-digit Decimal, 4096-bit modular arithmetic, complex identity, FP64 linear solve and FFT |
| Image classifier + RAM | PASS, 275 repeated inferences over 30.44s, 256MiB RAM-pattern check, max sampled 52.095°C |
| GLES1 | PASS, clear/scissor pixel values and six-clear checks |
| GLES3 graphics | PASS, 4× MSAA, depth and stencil; zero pixel failures |
| Native GPU fences | PASS, 100 export/import/server-wait chains, exact pixels; repeated after final boot |
| Compute language limits | PASS, 11 expected compile accept/reject checks; these are not dispatch tests |
| Actual GPU compute | PASS, 1024-point FFT ×16, forward/inverse, five warm rounds; 12 dispatches/run, output/guard checks passed |
| Normal manual desktop | PASS, GPU session + restart, terminal persistence/reopen, Wofi launcher, fullscreen transitions and native Wayland client |
| Desktop render result | 709 frames /12.01s, **59.03fps**, 12,324 marker checks, `V3D 3.3`; package-managed `/usr` DRI paths verified |
| CPU desktop fallback | PASS, separately started and exited |
| Desktop cleanup | PASS, fbcon restored, dma-buf count zero after all three sessions |
| Networking | PASS, Ethernet ping, DNS resolution and HTTPS with CA verification |
| Normal-user HDMI audio | PASS, two-second native S32_LE playback, PCM state RUNNING and nonzero hardware pointer |
| Linux USB/input | USB keyboard enumerated; input interfaces present; physical key presses not exercised |

The first direct `hw:0,0` audio probe used S16_LE, which this device rejects:
it exposes **S32_LE**. Repeating with native 32-bit PCM passed. This was a test
format correction; no audio driver/configuration was changed. Physical sound
and picture quality still require a listener/viewer.

GPU FFT remains slower than CPU for this workload: warm median GPU resident
1.736ms, GPU with transfers 5.534ms, four-thread CPU 0.092ms. Correctness passed;
this run does not change the earlier compute performance recommendation.

## Preservation and limits

Final SHA256 checks match factory sectors 34–6143, KRN, DTB, splash, boot0,
boot1, all eight DATA files and the installed DRI module. No kernel fault,
GPU reset/MMU error, OOM, filesystem error or I/O error was found by the recorded
checks. Existing `drm_sched_entity_init: called with uninitialized scheduler`
warnings remain in GPU/desktop logs; this is not a warning-free-kernel claim.

This is a targeted regression after storage migration, not proof that every
hardware feature or program works. Wi-Fi association, Bluetooth pairing,
physical remote/keyboard/mouse interaction, external USB disks, browser video
playback, full graphics conformance, destructive reset/rollback tests and a
long endurance campaign were not exercised here. Available hardware, existing
configuration and user profiles were retained; no packages were installed.

## Evidence

`bare-metal-state.json`, `mon-*`, and boot excerpts cover the monitor gate;
`doom-state.json` and `doom-boot-proof.txt` cover the successful application
return. `linux-*` cover numerical/graphics/compute checks. `desktop-*` cover the
three sessions; `classifier-report.json` includes the repeated inference/RAM
check. `final-*` cover the post-Doom Linux gate, device state and preservation.
First attempts are retained separately. Raw serial logs and screenshots remain
in ignored `port/build/platform-check/20261026/` on the development PC.

Scripts are the actual session harnesses and depend on installed board tools,
private preflight inputs and the serial bridge; they are audit artifacts,
not generic test runners. `python3 analyze.py` verifies the recorded result
and evidence checksums offline, without contacting the board.
