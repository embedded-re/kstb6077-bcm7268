# Incomplete patch-0032 full run

The 37,654-case set is unchanged. Remaining feature groups run first,
followed by the long copy-image group and the 50 large compute/indirect
cases. The monitor stops on the first failing result or unexpected GPU
fault. This is not a completed conformance result.

The run stopped after 2,577 reported cases on
`dEQP-GLES31.functional.fbo.no_attachments.npot_size.127x127`:
`Framebuffer did not behave as expected`. There are two GPU resets, zero
MMU errors and zero kernel oopses. The attachment-free framebuffer failure remains
unresolved. The dependent full graphics regressions did not run.
