# Device tree files: which one is original

## `boot/original_dtb.dts`: the original

The vendor device tree BOLT was running when `docs/bolt/raw/rescue.txt` was
captured, dumped live with `dt show` (`DT_SIZE a53e` = 42,258 bytes), and
reproduced exactly. Its `chosen` node is stock:

```
chosen {
    stdout-path = "/rdb/serial@f040c000:115200";
};
```

It has no `bootargs`, no `no-map` and no `bl31` node. This is the tree to
trust for bare-metal work.

## `boot/dtb.dtb` / `boot/Decompiled_dtb.dts`: a patched variant

A later variant from the Linux port. Differences from the original:

- `chosen` has an injected kernel command line (`root=/dev/mmcblk1p9 ... initcall_debug`)
- extra root nodes `0x80000000{}`, `0x00000000{}`, `reg{}`, `memory{}`
- renamed USB nodes: `usb@`→`usb-phy@`, `ehci@`→`ehci_v2@`, `ohci@`→`ohci_v2@`, `bdc@`→`bdc_v2@`
- a different blob size (`0x9af7` vs the resident `0xa53e`)

## Other derivatives (not in the repo)

- `dtb_working_fixed.dts`: original bootargs + `init=/bin/sh` (USB rescue variant)
- `dtb_usb_debian.dts`: `root=/dev/sda2` + `no-map`/`bl31@7df00000` memory reservations (Debian-from-USB variant)

## Notes for Linux

- The GPU node's compatible is `brcm,bcm7268-v3d`, but upstream Linux's V3D
  driver matches `brcm,7268-v3d`, so it won't bind without a change.
- The PSCI node uses `cpu_on = <0xc4000003>` (the SMC64 function ID).
