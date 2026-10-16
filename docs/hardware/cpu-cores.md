# CPU cores: starting cores 1–3 from bare metal

The BCM7268 has four Brahma-B53 cores (MIDR `420f1000`). `go -64` starts
only core 0 (MPIDR `80000000`). The other three are powered off until
someone asks the PSCI monitor at EL3 (`0x06400000`) to start them: BOLT's
`smm64` on the `go -64` path, kboot's own monitor on the `boot -64 -el3`
path (["Our own CPU_ON"](#our-own-cpu_on-kboot-el3-tested-2026-10-16)).

Tested on the modified box on 2026-10-02 with
[`../evidence/smp/psci_cpu_on_probe.s`](../evidence/smp/psci_cpu_on_probe.s) (output: [`psci_cpu_on_output.txt`](../evidence/smp/psci_cpu_on_output.txt)).

## Summary

| | |
|---|---|
| Cores | 4 × Brahma-B53 (MIDR `420f1000`), MPIDR `80000000`–`80000003` |
| After `go -64` | only core 0 runs |
| Starting cores 1–3 | PSCI `CPU_ON` (`0xc4000003`) through `smc #0` from EL2; the PSCI monitor (smm64 or kboot's) lives at `0x06400000` |
| A started core | AArch64 EL2, MMU and caches off, no stack, no vector table; `x0` = 0 (the context id is **not** passed) |
| Tested | all three cores started and running at once |

## The PSCI calls (`smc #0` from EL2)

| Call | `x0` | Arguments | Result in `x0` |
|---|---|---|---|
| PSCI_VERSION | `0x84000000` | — | `2` = v0.2 |
| CPU_ON (SMC64) | `0xc4000003` | `x1` = target MPIDR (`1`, `2`, `3`), `x2` = entry address, `x3` = context id | `0` = started |
| AFFINITY_INFO (SMC64) | `0xc4000004` | `x1` = MPIDR, `x2` = 0 (this core only) | `0` on, `1` off, `2` on pending |
| PSCI_FEATURES | `0x8400000a` | — | `-1`: not in PSCI v0.2 |

Other return codes (PSCI spec): `-2` invalid parameters, `-4` already on, `-5`
on pending, `-6` internal failure, `-9` invalid address.

The target MPIDR is just the affinity: `x1 = 1` for core 1 (no need for bit 31).

## What a started core looks like (tested: all three cores)

| | Core 1 | Core 2 | Core 3 |
|---|---|---|---|
| AFFINITY_INFO before → after | `1` → `0` | `1` → `0` | `1` → `0` |
| MPIDR_EL1 | `80000001` | `80000002` | `80000003` |
| CurrentEL | `8` = **EL2** | EL2 | EL2 |
| SCTLR_EL2 | `30c50830` (MMU and caches off) | same | same |
| `x0` at entry | **`0`** | `0` | `0` |

- A new core starts at **EL2 in AArch64**, at the entry address given, with
  the MMU and caches off: the same state as core 0 after `go -64`.
- **smm64 doesn't pass the context id in `x0`** (it should, by the PSCI
  spec; `x3 = n` was given, `x0` arrived as `0`: its EL2 start clears
  `x0`–`x3`). kboot's monitor does pass it. Either way the core can find out
  which it is with `mrs x0, MPIDR_EL1` and `and x0, x0, #0xff`.
- It has no stack and no vector table: set `sp` and `VBAR_EL2` per core.
- With the MMU off on every core, RAM accesses are uncached, so the cores see
  each other's writes immediately. Once a core turns its MMU on, use the
  same page tables, mark shared RAM inner shareable (`SH = 11`, as in
  [`display.md`](display.md)), and use `dsb`/`dmb` around shared data (not tested with the
  MMU on yet).
- Each core stored a counter to uncached RAM in a tight loop. Steps per 10 ms
  fell as more cores ran: 19,693 (core 1 alone), 15,494 (with core 2),
  11,472 (with core 3). They share the path to RAM (inferred).

## PSCI's own messages on the console

`smm64` prints to UART0 while it starts a core, interleaved with whatever
core 0 prints:

```
PWR_UP-CPU1 OK
BOOT64
ADDR-CPU1  0000000006402678 @ RDB: 20452008
ON-CPU1  00000002 @ 2020508c
GO-CPU1 @ 0000000001000188
```

What these messages mean, read from smm64's CPU_ON code (handler at
`0x06401824`) and checked on the board: `RDB` addresses are bus addresses,
smm64 ORs `0xd0000000` into them, so `20452008` is `0xf0452008` and
`2020508c` is `0xf020508c`. `0x06402678` is smm64's entry for a new core
(it sets its EL3 stack, then the same EL2 start-up as for core 0). The
registers are in the next section.

## The registers that start a core

| Register | Core *n* | Before CPU_ON (cores 1–3) | What CPU_ON does |
|---|---|---|---|
| `0xf02050c4 + 4n` power switch | core 0 `3c001908` | `c24819c8`: bit 31 set = off | if bit 31 is set: sets bits 6, 7, 5, 4 one at a time, waits for bit 29, sets bit 0, waits for bit 28, clears bit 6, sets bit 1. smm64 waits up to 50,000 × 10 µs per bit |
| `0xf0452000 + 8n` boot address, bits 39–32 | core 0 `00000000` | `00000000` | entry bits 39–32 |
| `0xf0452004 + 8n` boot address, bits 31–0 | core 0 `ffe00000` | `ffff0000` | entry bits 31–0 |
| `0xf020508c` reset | bits 1–3 | `000002fe`: bits 1–3 set = cores 1–3 held in reset | clears bit *n*: the core starts at EL3, AArch64, at the boot address |

Read on the box after a boot on the EL3 path (2026-10-16). After CPU_ON of
core 1: power switch `3a4819bb` (bit 31 clear, bits 28 and 29 set),
`0xf020508c` = `000002fc`, boot address `00000000 06400914`. smm64's CPU_OFF
path sets the reset bit again (`0x06401818`, not tested).

## Our own CPU_ON (kboot EL3, tested 2026-10-16)

kboot's EL3 monitor ([`../../kboot/el3.S`](../../kboot/el3.S),
[`../booting.md`](../booting.md) "Our own EL3") answers CPU_ON and
AFFINITY_INFO the same way, with the registers above. The new core starts in
the monitor (`el3_warm`, `0x06400914`), takes its EL3 stack (1 KB per core
below `0x06410000`), repeats core 0's set-up (CPUECTLR.SMPEN, CNTFRQ, its own
GIC registers, SCR_EL3, the EL2 registers, the timers) and drops to EL2 at
the entry. Per-core entry, context id and state are at `0x0640e000 + 32n`.

| Test | Result |
|---|---|
| [`psci_cpu_on_probe.s`](../evidence/smp/psci_cpu_on_probe.s), cores 1–3 | AFFINITY_INFO `1` → `0`, CPU_ON `0`; each core at EL2, MPIDR `8000000n`, SCTLR_EL2 `30c50830`; counter steps in 10 ms 19,750 / 15,509 / 11,470 (smm64: 19,693 / 15,494 / 11,472) |
| `x0` at the entry | **the context id** (`1`, `2`, `3`, as the PSCI spec says); smm64 passes `0` |
| from kmon (`call` of a stub), core 1 → a `wfe` loop | AFFINITY_INFO `1`, CPU_ON `0`, AFFINITY_INFO `0`, CPU_ON again `-4` (already on); kmon kept running |
| core 0, core 4, MPIDR `0x100` | CPU_ON core 0 `-4`, AFFINITY_INFO core 0 `0`; core 4 and `0x100` `-2` |

Not implemented: CPU_OFF, CPU_SUSPEND, MIGRATE. Two cores calling CPU_ON at
the same moment aren't locked against each other.

## Recipe

```asm
        ldr     x0, =0xc4000003       // CPU_ON
        mov     x1, #1                // core 1
        adr     x2, secondary         // entry
        mov     x3, #0                // context: x0 at entry (kboot's monitor; smm64 passes 0)
        smc     #0                    // x0 = 0: started
        ...
secondary:                            // runs on core 1, EL2, MMU off
        mrs     x9, MPIDR_EL1
        and     x9, x9, #0xff         // which core am I?
        // set sp (own stack!), VBAR_EL2, then work
```

## Open questions

- Turning a core off again (`CPU_OFF` `0x84000002`, called by that core) and
  restarting it.
- Caches and the MMU on several cores at once (coherency).
- Interrupts on the other cores (GIC CPU interfaces, `ITARGETSR`).
