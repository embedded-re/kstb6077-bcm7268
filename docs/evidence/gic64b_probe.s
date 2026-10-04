// AArch64 probe: CPU state + GIC registers. Leaf helpers only (no stack used).
.global _start
_start:
    mov     x20, #0xc000
    movk    x20, #0xf040, lsl #16          // x20 = UART0
    mov     x21, #0x1000
    movk    x21, #0xffd0, lsl #16          // x21 = GICD 0xffd01000
    add     x22, x21, #0x1000              // x22 = GICC 0xffd02000

    adr x0, s_hdr;   bl puts
    mrs x19, CurrentEL
    ubfx x19, x19, #2, #2
    adr x0, s_el;    bl puts; mov x0, x19;  bl puthex
    adr x0, s_midr;  bl puts; mrs x0, MIDR_EL1;  bl puthex
    adr x0, s_mpidr; bl puts; mrs x0, MPIDR_EL1; bl puthex
    adr x0, s_frq;   bl puts; mrs x0, CNTFRQ_EL0; bl puthex
    cmp x19, #3
    b.eq 1f
    adr x0, s_sct2;  bl puts; mrs x0, SCTLR_EL2; bl puthex
    adr x0, s_hcr;   bl puts; mrs x0, HCR_EL2;   bl puthex
    adr x0, s_vbr2;  bl puts; mrs x0, VBAR_EL2;  bl puthex
    b 2f
1:  adr x0, s_sct3;  bl puts; mrs x0, SCTLR_EL3; bl puthex
    adr x0, s_scr;   bl puts; mrs x0, SCR_EL3;   bl puthex
    adr x0, s_vbr3;  bl puts; mrs x0, VBAR_EL3;  bl puthex
2:
    adr x0, s_dctlr; bl puts; ldr w0, [x21, #0x000]; bl puthex
    adr x0, s_dtyp;  bl puts; ldr w0, [x21, #0x004]; bl puthex
    adr x0, s_diid;  bl puts; ldr w0, [x21, #0x008]; bl puthex
    mov x23, #0
5:  adr x0, s_grpn;  bl puts; add x24, x21, #0x080; ldr w0, [x24, x23]; bl puthex
    add x23, x23, #4
    cmp x23, #32
    b.lo 5b
    mov x23, #0
6:  adr x0, s_enan;  bl puts; add x24, x21, #0x100; ldr w0, [x24, x23]; bl puthex
    add x23, x23, #4
    cmp x23, #32
    b.lo 6b
    adr x0, s_ena3;  bl puts; ldr w0, [x21, #0x10c]; bl puthex
    adr x0, s_pnd3;  bl puts; ldr w0, [x21, #0x20c]; bl puthex
    adr x0, s_tgt;   bl puts; ldr w0, [x21, #0x860]; bl puthex
    adr x0, s_cctl;  bl puts; ldr w0, [x22, #0x000]; bl puthex
    adr x0, s_ciid;  bl puts; ldr w0, [x22, #0x0fc]; bl puthex
    adr x0, s_done;  bl puts
3:  wfe
    b 3b

// puts: x0 -> NUL-terminated string. Clobbers x0-x3.
puts:
    ldrb    w1, [x0], #1
    cbz     w1, 9f
8:  ldr     w2, [x20, #0x14]
    tbz     w2, #5, 8b
    str     w1, [x20]
    b       puts
9:  ret

// puthex: prints x0 as 0x + 16 hex digits + CRLF. Clobbers x0-x4.
puthex:
    mov     x3, x0
    mov     x4, #60
1:  lsr     x1, x3, x4
    and     x1, x1, #0xf
    cmp     x1, #10
    add     x2, x1, #'0'
    add     x1, x1, #('a' - 10)
    csel    x1, x2, x1, lo
2:  ldr     w2, [x20, #0x14]
    tbz     w2, #5, 2b
    str     w1, [x20]
    subs    x4, x4, #4
    b.pl    1b
    mov     w1, #'\r'
3:  ldr     w2, [x20, #0x14]
    tbz     w2, #5, 3b
    str     w1, [x20]
    mov     w1, #'\n'
4:  ldr     w2, [x20, #0x14]
    tbz     w2, #5, 4b
    str     w1, [x20]
    ret

s_hdr:   .asciz "\r\n== gic64b probe ==\r\n"
s_el:    .asciz "CurrentEL   "
s_midr:  .asciz "MIDR_EL1    "
s_mpidr: .asciz "MPIDR_EL1   "
s_frq:   .asciz "CNTFRQ_EL0  "
s_sct2:  .asciz "SCTLR_EL2   "
s_hcr:   .asciz "HCR_EL2     "
s_vbr2:  .asciz "VBAR_EL2    "
s_sct3:  .asciz "SCTLR_EL3   "
s_scr:   .asciz "SCR_EL3     "
s_vbr3:  .asciz "VBAR_EL3    "
s_dctlr: .asciz "GICD_CTLR   "
s_dtyp:  .asciz "GICD_TYPER  "
s_diid:  .asciz "GICD_IIDR   "
s_grp3:  .asciz "IGROUPR3    "
s_grpn:  .asciz "IGROUPRn    "
s_enan:  .asciz "ISENABLERn  "
s_ena3:  .asciz "ISENABLER3  "
s_pnd3:  .asciz "ISPENDR3    "
s_tgt:   .asciz "ITARGETSR96 "
s_cctl:  .asciz "GICC_CTLR   "
s_ciid:  .asciz "GICC_IIDR   "
s_done:  .asciz "== done ==\r\n"
