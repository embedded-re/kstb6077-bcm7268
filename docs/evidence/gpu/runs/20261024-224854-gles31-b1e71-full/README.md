# Incomplete full GLES 3.1 run

The monitor intentionally stopped this run on the first failing result.
5,292 cases reported: 5,133 Pass, 158 NotSupported, one Fail. This is not a
full conformance result. Kernel capture reports zero MMU errors, resets or
oopses. The runner's `end` marker records shutdown, not completion of all
37,654 requested cases.

The failure is
`dEQP-GLES31.functional.copy_image.non_compressed.viewclass_128_bits.rgba32i_rgba32i.renderbuffer_to_renderbuffer`:
`Image comparison failed.` Some already-running cases reported after it
before the monitor interrupted the process. The failure is unresolved.

The follow-up queue stopped because the full-run gate failed. It did not
execute its neural-network or graphics regression stages.
