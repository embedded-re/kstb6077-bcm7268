# Idea: assembly second-stage bootloader before Linux

**Status:** untested idea (2026-09-30). The logs from the abandoned Linux port
are gone, so the original IRQ failure is not documented. It is unknown whether
it came from the DTB or from hardware state.

## Concept

Put a small assembly shim between BOLT and Linux:

```
BOLT → load shim + zImage + DTB → go <shim>
     → shim: inspect / fix hardware state → hand off to Linux
```

The shim would reuse the existing monitor pieces: UART output, `build.sh`, and
the BOLT `load` + `go` flow.

## What it can and cannot fix

**Cannot fix: anything Linux sets up itself.** On boot, the Linux GIC driver
reinitialises the distributor: it disables every SPI and sets priorities,
targets and trigger types from the DTB. The Broadcom L2 controllers also have
a mainline driver. For all of that, the DTB is the only lever. Example: the
earlier `IRQ_TYPE_NONE` → `LEVEL_HIGH` fix.

**Might fix (needs testing):**

1. **GIC security groups.** If Linux runs non-secure and some interrupts are
   left in the secure group, Linux cannot see or configure them, whatever the
   DTB says. Only code running secure before Linux can move them to the
   non-secure group. BOLT installs a secure monitor (`INSTALL smm64@06400000`),
   so this is plausible, but **unverified**.
2. **Clocks, power and pin mux with no Linux driver.** The port replaced the
   19 `brcm,brcmstb-sw-clk` clocks with fake `fixed-clock` nodes, which may
   describe clocks that are actually off. A shim could really enable those
   blocks. It might also explain the USB deferred probe, which is a guess.
3. **A controlled handoff.** ARM Linux boot requirements: MMU off, D-cache
   cleaned and off, IRQ/FIQ masked, `r0 = 0`, `r1 = 0xffffffff`,
   `r2 = DTB address`, SVC (or HYP) mode.

## Update (2026-09-30): BOLT can start the shim at EL3 ✅

`boot -64 -el3 …` launched a test program **as the secure monitor at EL3**
(AArch64), and `go -64` launches at EL2 (details in `../bolt/bolt.md` §9). That
removes the biggest unknown in point 1 above: a shim started this way runs
secure, so it can move GIC interrupts into the non-secure group before
dropping to Linux. The costs: the shim must be AArch64 code, and it takes
over from BOLT's PSCI/BL31. It would have to provide PSCI itself (CPU_ON for
SMP), or the handoff must stay on BOLT's path.

The simpler alternative is `go -64` (EL2, BOLT's PSCI stays in charge). A
64-bit Linux kernel could then be tried directly, but the GIC group question
stays unanswered from below EL3.

## Update (2026-09-30): the GIC security-group theory is ruled out ❌

Read from EL3 (secure view): **IGROUPR1–7 = `ffffffff`, so every SPI (IDs
32–255) is already in group 1 (non-secure)**. Only PPI slots 16–24 are
secure. The GIC distributor and CPU interface are enabled for both groups.
A non-secure Linux kernel can see and configure every peripheral interrupt,
so point 1 above was **not** the cause of the old Linux IRQ problem.
Details: `../hardware/interrupts.md` ("GIC state at handoff").

What remains plausible: point 2 (clocks, power or pin mux that Linux has no
driver for) and DTB errors. For those a shim doesn't need EL3: `go -64` (EL2)
or a 32-bit shim is enough.

## Useful side effect

Turning the MMU off for the handoff also makes the GIC reachable
(`0xffd01000` / `0xffd02000`). BOLT's page tables don't map it; see
`../hardware/memory-map.md`. The shim doubles as the tool for inspecting GIC state.

## Test plan

1. **Minimal shim:** print a banner, clean and disable caches and the MMU,
   jump to the zImage with the correct registers. Success means Linux boots
   exactly as it does with a plain `go`.
2. **Inspection pass:** with the MMU off, dump GICD_CTLR, GICD_TYPER, the
   IGROUPR/ISENABLER/ITARGETSR registers and the CPU interface. Check whether
   writes to IGROUPR stick; that shows whether the shim runs secure.
3. **Linux side:** boot Linux 6.6 and save `dmesg` and `/proc/interrupts`
   this time, so the IRQ failure is actually recorded.
4. **Decide on the fix only after 1–3**: move interrupts to the non-secure
   group, enable clocks or power, or conclude it's a DTB problem after all.

## Constraints to respect

- Don't overwrite BOLT's PSCI/secure-monitor memory at `0x06400000`. Linux
  6.6 used PSCI v0.2 for SMP.
- Known load addresses: DTB `0x07700000`, zImage `0x02208000`, monitor
  `0x01000000`.
- Debug aids already mapped: LED3 (AON bank 0 bit 16) for stage markers, SW4
  (bit 7) to hold or abort the handoff.
