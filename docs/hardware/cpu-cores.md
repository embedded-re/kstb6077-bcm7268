# CPU cores: starting cores 1–3 from bare metal

The BCM7268 has four Brahma-B53 cores (MIDR `420f1000`). `go -64` starts
only core 0 (MPIDR `80000000`). The other three are powered off until
someone asks the PSCI monitor (`smm64`, EL3, resident at `0x06400000`) to
start them.

Tested on the modified box on 2026-10-02 with
[`../evidence/smp/psci_cpu_on_probe.s`](../evidence/smp/psci_cpu_on_probe.s) (output: [`psci_cpu_on_output.txt`](../evidence/smp/psci_cpu_on_output.txt)).

## Summary

| | |
|---|---|
| Cores | 4 × Brahma-B53 (MIDR `420f1000`), MPIDR `80000000`–`80000003` |
| After `go -64` | only core 0 runs |
| Starting cores 1–3 | PSCI `CPU_ON` (`0xc4000003`) through `smc #0` from EL2; the PSCI monitor `smm64` lives at `0x06400000` |
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
- **The context id is not passed in `x0`** (it should be, by the PSCI
  spec; `x3 = n` was given, `x0` arrived as `0`). Identify the core with
  `mrs x0, MPIDR_EL1` and `and x0, x0, #0xff`.
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

Inferred meaning: `0x06402678` is smm64's warm-boot entry, written to a
per-CPU boot-address register (`RDB` = Broadcom's register database;
`0x20452008/10/18` for CPUs 1–3), and `ON-CPUn` sets bit *n* (`2`/`4`/`8`)
of a power-control register at `0x2020508c`. Then the core jumps to the
given entry (`GO-CPUn`). These look like bus addresses with a different base
than `0xf…`, and were not read.

## Recipe

```asm
        ldr     x0, =0xc4000003       // CPU_ON
        mov     x1, #1                // core 1
        adr     x2, secondary         // entry
        mov     x3, #0                // context (not passed on this firmware)
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
