# Browser/build cache cleanup — 2026-10-26

User authorized removal of our temporary Firefox test profiles, browser cache
and Cargo registry cache. Freed **833,875,968 bytes (about 795MiB)**; root usage
fell from 81% to 65%, with `df -h` reporting 1.8G available afterward.

Ten explicitly identified demo/isolation/test profiles were archived first to
private ignored PC storage at
`linux/build/cache-cleanup/20261026/private-test-profiles.tar.gz` (directory
mode 0700, archive 0600). Full gzip integrity passed. The archive is private
browser data and must not be committed or published. Its checksum is retained.

The two registered profiles, `cpvdr4gu.default-release` and `hg7bega6.default`,
and profiles.ini/installs.ini were content-hashed before/after and unchanged.
Profile Groups metadata, bookmarks/history, passwords and normal user settings
were preserved. No Firefox, Cargo or rustc process was running; targets were
checked for mappings/open files before removal. Browser cache was recreated
with user kstb ownership. Only `/.cargo/registry` was removed: Cargo binaries,
configuration and compiler installations remain. Downloads will regenerate
when needed. Installed apps, GPU/kernel files and recovery backups were kept.

`result.json` records the exact removed paths and filesystem free-space delta.
No GPU workload, process stress or system upgrade was performed.
