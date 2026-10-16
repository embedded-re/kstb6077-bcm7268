# What EL3 sees that EL2 doesn't

Code at EL3 (the secure world) and code at EL2 ran the same read-only probes
on the modified box on 2026-10-16, to find what only EL3 can read. Both from
`BOLT>` (STARTUP skipped, so BOLT's smm64 is the EL3 monitor and kboot is not
involved), same binary at both levels, MMU off:

```
boot -64 -el3 -raw -tftp -addr=0x03000000 <PC>:<probe>.bin    EL3 ("64 bit PSCI (@ EL3) boot...")
boot -64 -raw -tftp -addr=0x03000000 <PC>:<probe>.bin         EL2 ("64 bit PSCI boot...")
```

Each probe prints `CurrentEL` first (`3` and `2`), sets its own vectors
(`VBAR_EL3` or `VBAR_EL2`) and routes aborts and SError to its own level
(`SCR_EL3.EA` at EL3, `HCR_EL2.AMO` at EL2). A read that faults prints
`SYNC <ESR>`.

| Probe | What it reads | Outputs |
|---|---|---|
| [`el3_read_probe.s`](../evidence/el3/el3_read_probe.s) | 34 system registers; 152 single words: the [`periph_probe.s`](../evidence/periph/periph_probe.s) list, every EL2 abort hole known so far, OTP, GIC registers | [EL3](../evidence/el3/el3_read_probe_el3_output.txt), [EL2](../evidence/el3/el3_read_probe_el2_output.txt) |
| [`el3_dump_probe.s`](../evidence/el3/el3_dump_probe.s) | every word of: GIC distributor `0xffd01000–0xffd01fff`, BSP `0xf032c800–0xf032ffff` (DTB `bsp`), memory controller `0xf1100000–0xf1107fff`, DTU config `0xf1109000–0xf11094ff`, DTU map `0xf1110000–0xf1117fff` | EL3 [1](../evidence/el3/el3_dump_probe_el3_output.txt) [2](../evidence/el3/el3_dump_probe_el3_output_2.txt), EL2 [1](../evidence/el3/el3_dump_probe_el2_output.txt) [2](../evidence/el3/el3_dump_probe_el2_output_2.txt) |

## Summary

| | EL2 | EL3 |
|---|---|---|
| Peripheral registers (all words above) | | **the same**: no word reads at EL3 that aborts at EL2, and the reverse |
| GIC | non-secure view | secure view: group settings, secure priority mask ([below](#gic-the-only-memory-that-reads-differently)) |
| EL3 system registers | UNDEFINED (`SYNC 02000000`) | readable ([below](#system-registers)) |
| Cortex-A53 implementation-defined registers | only CPUECTLR tried (`00000040`) | all 7 readable |

## Peripheral registers: no difference

Every word that aborts at EL2 also aborts at EL3, with the same ESR
`96000210` (synchronous external abort), and every word that reads at EL2
reads at EL3:

| Where | Result at both levels |
|---|---|
| AON `0xf041002c`; `0xf041006c` | abort; reads `00000000` |
| GFD `0xf064105c` | abort |
| USB control block `0xf0b00280–0x28c`, `0x29c`, `0x2b8–0x2ec` | abort |
| HDMI TX `0xf06fa27c`, `300`, `6f4`, `700`, `878`, `8bc`, `930`, `9b0`, `a30`, `ab0`, `e00` (first word of each hole) | abort |
| Clock generator, first word of each of the 8 aborting runs (`0xf04e0528` … `0xf04e6160`) | abort |
| `0xf0460000`, `0xf0b02000`, `0xf1201000`, `0xf0402800` | abort |
| OTP `0xf0404030` / `34` / `0xf0404520` | `00000040` / `00a02000` / `00000000` |
| BSP `0xf032c800–0xf032ffff` | **934** words abort, the same words |
| Memory controller `0xf1100000–0xf1107fff` | **6,689** words abort, the same words |
| DTU config `0xf1109000–0xf11094ff` | **220** words abort, the same words |
| DTU map `0xf1110000–0xf1117fff`, GIC distributor | none abort |

Words whose value differed between the EL2 and EL3 runs also differ between
two runs at the same level, so they are live state, not security:
`0xf032d004`, `0xf1100040–0x58`, `0xf1101424`, `0xf1101488` (two runs at
each level), `0xf04d1200` (AVS interrupt status: `04000000` at EL2,
`04000002` at EL3, one run each).

## GIC: the only memory that reads differently

GIC-400 registers that differ (all other distributor words, `0xffd01000–0xffd01fff`, are equal):

| Register | EL2 | EL3 |
|---|---|---|
| GICD_CTLR `0xffd01000` | `00000001` | `00000003` |
| GICD_IGROUPR0–7 `0xffd01080–0x9c` | `00000000` | `fe00ffff`, then `ffffffff` × 7 |
| GICC_CTLR `0xffd02000` | `00000001` | `00000003` |
| GICC_PMR `0xffd02004` | `00000000` | `00000080` |
| GICC_BPR `0xffd02008` | `00000003` | `00000002` |
| GICC_ABPR `0xffd0201c` | `00000000` | `00000003` |
| GICC_AHPPIR `0xffd02028` | `00000000` | `000003ff` |

Equal at both: GICD_TYPER `0000fc67`, GICD_IIDR `0200143b`, ISENABLER0
`0000ffff`, IPRIORITYR0–7 `0`, ITARGETSR0–7, ICFGR0–1 `aaaaaaaa`
`55540000`, NSACR0–1 `0`, GICC_HPPIR `000003ff`, APR0, NSAPR0 `0`,
GICC_IIDR `0202143b`, `0xffd04000` / `04` `00000000` / `90000003`.
GICC_IAR and GICC_AIAR were not read (a read acknowledges an interrupt).

## System registers

Read with `mrs` at entry (before the probe changed anything except its own
`VBAR` and the abort routing bit). At EL2 every EL3 register traps as
UNDEFINED (`ESR 02000000`).

| Register | EL3 | EL2 |
|---|---|---|
| SCR_EL3 (at entry) | `131` | UNDEFINED |
| SCTLR_EL3 | `00c52838` | UNDEFINED |
| ACTLR_EL3 | `00000002` | UNDEFINED |
| MDCR_EL3, CPTR_EL3, TCR_EL3 | `0`, `0`, `0` | UNDEFINED |
| **RVBAR_EL3** (reset vector address) | **`fffd0000`** | UNDEFINED |
| VBAR_EL3 (at entry) | `06400000` | UNDEFINED |
| TTBR0_EL3, MAIR_EL3 | `00f77feff4ebafeb`, `44e048e000098aa4` | UNDEFINED |
| ELR_EL3, SPSR_EL3 (at entry) | `07011c7c`, `600000d3` | UNDEFINED |
| CNTPS_CTL_EL1, CNTPS_CVAL_EL1 (secure physical timer) | `0`, `0c40ff991484008b` | UNDEFINED |
| CPUACTLR_EL1 `S3_1_C15_C2_0` | `090ca000` | not tried |
| CPUECTLR_EL1 `S3_1_C15_C2_1` | `00000040` | `00000040` |
| CPUMERRSR_EL1 `S3_1_C15_C2_2` | `191003ff` | not tried |
| L2MERRSR_EL1 `S3_1_C15_C2_3` | `10000020` | not tried |
| L2CTLR_EL1 `S3_1_C11_C0_2` | `03600021` | not tried |
| L2ECTLR_EL1 `S3_1_C11_C0_3` | `00000000` | not tried |
| L2ACTLR_EL1 `S3_1_C15_C0_0` | `80000000` | not tried |
| CBAR_EL1 `S3_1_C15_C3_0` | `ffd00000` | not tried |

The implementation-defined registers other than CPUECTLR were not tried at
EL2: ACTLR_EL3 = `2` (only the CPUECTLR bit set), so a read from EL2 may trap
to EL3, into smm64.

Equal at both levels: MIDR `420f1000`, MPIDR `80000000`, REVIDR `180`,
ID_AA64PFR0 `2222`, ID_AA64DFR0 `10305106`, ID_AA64ISAR0 `11120`,
ID_AA64MMFR0 `1122`, CNTFRQ `019bfcc0`, SCTLR_EL2 `30c50830`, ACTLR_EL2 `2`,
CPTR_EL2 `33ff`, CNTHCTL_EL2 `3`, ACTLR_EL1 `0`. MDCR_EL2 is `6` at EL3
entry and `1f` at EL2 entry (smm64 sets it before it drops to EL2,
[`../booting.md`](../booting.md) "Our own EL3").

## Not read yet

- Secure DRAM `0x7df00000–0x7fffffff` (BL31, SRR): a read could trip the
  memory controller's protection and hang the board; needs the user's go.
