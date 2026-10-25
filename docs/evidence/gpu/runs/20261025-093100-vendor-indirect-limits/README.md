# Vendor V3D 3.3 indirect-primitive limits

`run.py` executes the stock driver functions in a host Unicorn ARM/Thumb
model. It performs no device I/O. `result.json` records the binary hash and
the measured values.

- `v3d_maybe_desc_cl_opcode` identifies opcode **44** as
  `indirect_primitive_limits`; `v3d_cl_instr_size` returns **12 bytes**.
- The real packer emits an 11-byte payload: `max_index` occupies 24 bits,
  `max_instance` the next 32 bits, and `index_buffer_size` the final 32 bits.
- The modeled printer callbacks capture those exact field names and recover
  all three test values from the payload.
- The new Mesa XML packet matches the executed vendor packer byte for byte;
  see [mesa-xml-check.txt](mesa-xml-check.txt).

Mesa previously defined opcode 44 only for V3D 4.1+ `Index Buffer Setup`.
The added packet is restricted to V3D 3.3. Native indexed and nonindexed
indirect draws now emit these limits. The hardware test result is
[157/157 normal-sized indirect cases](../20261025-092549-gles31-indirect-limits-short/summary.txt).

The script needs the host `unicorn` and `pyelftools` modules and runs from
the repository root with the stock binary present.
