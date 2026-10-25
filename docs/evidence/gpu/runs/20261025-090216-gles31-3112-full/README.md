# Incomplete patch-0031 full run

The unchanged 37,654-case set was reordered to run non-copy features before
the long copy-image sweep. The monitor stopped on the first failing group:
2,305 cases reported, with 1,955 Pass, 344 NotSupported, five Fail and one
QualityWarning. No MMU error, GPU reset or oops occurred. This is not a full
conformance result.

The five failures are combined compute/indirect-draw interoperability cases.
Independent controls subsequently show that indirect draws with CPU-filled
buffers also fail, and an explicit compute-completion wait does not fix the
render. Patch 0032 adds the missing native indirect-primitive-limits packet;
157 normal-sized indirect cases then pass.
