# Baseline skip and warning inventory

The inventory covers all **3,940 non-Pass outcomes** in the archived kernel
#29 / Mesa `848c0204` runs: GLES3 860, GLES2 124, EGL 2,956. Every case is
assigned once by the generator; counts and input hashes are in
[reasons.md](reasons.md). This inventories the recorded conditions. It does
not establish that each omitted feature is a hardware limit or that the
remaining warnings are acceptable.

| Suite | Count | Recorded condition | Remaining question |
| --- | ---: | --- | --- |
| GLES3 | 333 NS | Transform-feedback implementation limits exceeded | Which queried limit excludes each program, and does that limit match the silicon/vendor? |
| GLES3 | 168 NS | Missing extensions: indexed draw buffers 79, advanced blend 10, sRGB R8/RG8 64, half-float color buffers 12, multiview 3 | Determine hardware support or required software support for each extension. |
| GLES3 | 61 NS | Sample restrictions: unsupported sample count 26, no default-framebuffer MSAA 16, RGBA8 limited to four samples 16, float32 MSAA 3 | Separate the selected window configuration from format-specific driver limits. |
| GLES3 | 21 NS | Separate depth/stencil attachments 12, other unsupported FBO formats 9 | Check attachment/storage restrictions and the exact formats. |
| GLES3 | 270 QW | Low-quality shadow-texture results | Investigate comparison filtering; no root cause established by this inventory. |
| GLES3 | 7 QW | Projected-coordinate interpolation | Determine whether precision or interpolation behavior causes the warning. |
| GLES2 | 12 NS | Missing clip-control, debug-marker and multisampled-render-to-texture extensions | The vendor advertises multisampled render to texture; implementation work remains. |
| GLES2 | 16 NS | No default-framebuffer multisample buffers | Selected test-window configuration. Broader MSAA coverage remains necessary. |
| GLES2 | 91 NS | Generic `Not supported`; every case requests separate depth16 and stencil8 attachments | The case names identify the attachment combination; the generic reason alone does not prove the cause. |
| GLES2 | 5 QW | Projected-coordinate interpolation | Same warning family as GLES3; cause remains open. |
| EGL | 2,442 NS | No suitable configuration or unsupported window-surface/config combination | Compare required attributes with all offered EGL configs. |
| EGL | 378 NS | Missing extensions, including 200 generic create-context extension skips | Identify the exact missing combination for generic messages; compare with vendor capabilities. |
| EGL | 78 NS | Native-window helpers unavailable: readback, platform-window creation, resizing or screen-size query | Test-platform limitations; expand the GBM adapter where useful. |
| EGL | 52 NS | Pixel-readback format disallowed or EGLImage-to-texture path unavailable | Distinguish API format restrictions from missing import support. |
| EGL | 1 NS | Test requires GLES 3.1 | Baseline exposes GLES 3.0. Recheck on the opt-in 3.1 candidate. |
| EGL | 4 NS | Generic `Not Supported` | Unresolved; inspect individual tests. |
| EGL | 1 QW | Non-null address for an unsupported function | Check dispatch lookup and the tested extension advertisement. |

The 270 shadow warnings split evenly across 2D, cube and array textures.
The 12 interpolation warnings comprise seven GLES3 and five GLES2 cases.
These counts come from case names and recorded CTS messages, without
rerunning the GPU.

The CTS transform-feedback check at
`modules/gles3/functional/es3fTransformFeedbackTests.cpp:254` tests vertex
attributes as well as interleaved components, separate attributes and
separate components. Consequently, labeling all 333 skips as one specific
transform-feedback component limit would be unjustified.

Regenerate from the repository root:

```sh
python3 linux/gpu-test/deqp/result-reasons.py \
  docs/evidence/gpu/runs/20261022-220306-gles3-848c-krn29-full/results-gles3.txt \
  docs/evidence/gpu/runs/20261022-225849-gles2-848c-krn29-full/results-gles2.txt \
  docs/evidence/gpu/runs/20261022-231119-egl-848c-krn29-full/results-egl.txt
```

Add `--json` for each group's complete case list. The generator rejects
malformed rows and duplicate case names and includes every non-Pass status,
including failures, should they occur in a later input.
