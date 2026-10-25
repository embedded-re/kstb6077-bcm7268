"""Host-only execution of vendor CL description/packing functions; no device IO."""
from pathlib import Path
import hashlib,json,struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
path=Path('stock/release/hal/egl/libGLES_nexus.so')
u=Uc(UC_ARCH_ARM,UC_MODE_THUMB);u.mem_map(0,0x400000)
with path.open('rb') as f:
 for seg in ELFFile(f).iter_segments():
  if seg['p_type']=='PT_LOAD':u.mem_write(seg['p_vaddr'],seg.data())
def call(pc,r0,r1=0):
 u.reg_write(UC_ARM_REG_R0,r0);u.reg_write(UC_ARM_REG_R1,r1)
 u.reg_write(UC_ARM_REG_SP,0x3e0000);u.reg_write(UC_ARM_REG_LR,0x3f0001)
 u.emu_start(pc|1,0x3f0000,count=10000)
 assert u.reg_read(UC_ARM_REG_PC)==0x3f0000
 return u.reg_read(UC_ARM_REG_R0)
def string(p):return bytes(u.mem_read(p,128)).split(b'\0')[0].decode()
opcodes=[]
for op in range(127):
 p=call(0xaade4,op)
 if p and 'indirect' in string(p):opcodes.append(dict(opcode=op,name=string(p),bytes=call(0xb3494,op)))
assert dict((x['name'],(x['opcode'],x['bytes'])) for x in opcodes)['indirect_primitive_limits']==(44,12)
values=[0x123456,0x89abcdef,0x13579bdf]
u.mem_write(0x330000,struct.pack('<3I',*values));call(0xb7694,0x331000,0x330000)
payload=bytes(u.mem_read(0x331000,11))
assert payload==struct.pack('<I',values[0])[:3]+struct.pack('<2I',*values[1:])
# Model the printer callbacks to collect the actual field names and values.
u.mem_write(0x332000,struct.pack('<I',0x333000))
u.mem_write(0x333000,struct.pack('<3I',0x3c0001,0x3c0101,0x3c0201))
fields=[]
def hook(uc,pc,size,user):
 if pc not in [0x3c0000,0x3c0100,0x3c0200]:return
 if pc==0x3c0200:fields.append(dict(name=string(uc.reg_read(UC_ARM_REG_R1)),value=uc.reg_read(UC_ARM_REG_R3)))
 uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
u.hook_add(UC_HOOK_CODE,hook);call(0xb43a0,0x331000,0x332000)
assert [x['name'] for x in fields]==['max_index','max_instance','index_buffer_size']
assert [x['value'] for x in fields]==values
print(json.dumps(dict(binary=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),method='host Unicorn execution; printer callbacks modeled; no MMIO',opcodes=opcodes,packed_payload=payload.hex(),fields=fields),indent=2))
