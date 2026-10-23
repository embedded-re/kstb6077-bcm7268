# dEQP on the KSTB6077

The configured cross-build is in `linux/build/deqp`, using VK-GL-CTS
`opengl-es-cts-3.2.14.1`, the AArch64 Alpine toolchain and the GBM null-window
patch in this directory. The GLES3.1 binary builds with:

```sh
ninja -C linux/build/deqp/build -j4 deqp-gles31
```

Package `modules/gles31/deqp-gles31` and its sibling `gles31/` data directory
under `/opt/deqp/gles31/`, following the existing GLES2/GLES3 layout. Strip a
copy of the executable with `llvm-strip`; record the deployed copy's hash in
`/opt/deqp/SHA256SUMS`.

Generate the GLES3.1 main case list from the checkout's Android manifest:

```sh
python linux/gpu-test/deqp/gles31-main.py linux/build/deqp/src \
    linux/build/deqp/pkg/deqp/mustpass/gles31-main.txt \
    > linux/build/deqp/gles31-mustpass.json
```

The script preserves manifest order and removes duplicate names. On this
CTS tag, the five dated lists contain 37,654 distinct cases; the 2024 list
alone contains only 34 additions. Its JSON output records every source hash
and the resulting list hash. This is a union of case names; the runner uses
its GBM window configuration. Existing GLES2/GLES3 case lists are full tree
exports and should retain their broader coverage.

`kstb-gpu-run gles31` accepts this layout and list. Board validation of this
new module is pending. A version override does not establish working
GLES3.1 support; compute and image features need separate implementation
and hardware tests before being exposed by default.
