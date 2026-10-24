#!/usr/bin/python3
# Raw compute probe: read tile IDs, derive an output address, store a uvec4.
# No bin/vertex shader or framebuffer output. Kernel cache clean is requested.
# The CPU checks all records, inactive records, and both buffer guards.
import ctypes, fcntl, mmap, os, struct, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from v3dpack import pack

def iowr(nr, size):
    return (3 << 30) | (size << 16) | (ord('d') << 8) | (0x40 + nr)

SUBMIT_CL, WAIT_BO, CREATE_BO, MMAP_BO = iowr(0, 72), iowr(1, 16), iowr(2, 16), iowr(3, 16)
fd = os.open('/dev/dri/renderD128', os.O_RDWR)

def ioctl(req, fmt, *args):
    return struct.unpack(fmt, fcntl.ioctl(fd, req, struct.pack(fmt, *args)))

class BO:
    def __init__(self, size):
        self.size = size
        _, _, self.handle, self.va = ioctl(CREATE_BO, '<IIII', size, 0, 0, 0)
        off = ioctl(MMAP_BO, '<IIQ', self.handle, 0, 0)[2]
        self.m = mmap.mmap(fd, size, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE, offset=off)

    def put(self, off, data):
        self.m[off:off + len(data)] = data

    def words(self, off, n):
        return struct.unpack_from('<%dI' % n, self.m, off)

def hexwords(s):
    return [int(x, 16) for x in s.split()]


cl=BO(65536);source=BO(20480);color=BO(20480);output=BO(40960)
bos=[cl,source,color,output]
RCL,REC,FS,UNIF,VERTS,ZW=0,0x400,0x1000,0x2000,0x3000,0x3400
lines=int(os.environ.get('LINES','16'));assert 1<=lines<=16
# Exact vendor compute_cl_begin bytes, emitted by host emulation.
r=bytes.fromhex('7900400020004100ff7902151804000000007901040300000000007903000000000000000b13491158615b0000803f5c0000000069000000406c00000000000000006d000000000000803f6b00000000400020006001700057ffff0000')
# Enable RT0 writes and point its output state at our mapped output BO.
r=pack('Tile Rendering Mode Cfg (Common)',number_of_render_targets=1,
 image_width_pixels=64,image_height_pixels=32,maximum_bpp_of_all_render_targets=1,
 early_z_disable=1,disable_render_target_stores=255)+r[9:]
r=r[:9]+pack('Tile Rendering Mode Cfg (Color)',render_target_number=0,
 internal_bpp=1,internal_type=5,output_image_format=24,memory_format=4,address=color.va)+r[18:]
r=r[:88]+pack('Color Write Masks',mask=0)
# Match Mesa's GFXH-1742 initialization after changing tile format.
r+=pack('Tile Coordinates')+pack('Store Tile Buffer General',buffer_to_store=8)
r+=pack('Flush VCD cache')
r+=pack('Load Tile Buffer General',address=source.va,raw_mode=1,buffer_to_load=0,
 padded_height_of_output_image_in_uif_blocks=8)
r+=pack('Tile Coordinates')+pack('Prim List Format',primitive_type=1)
# NV shader state: same address/attribute field layout as GL_SHADER, opcode68.
r+=bytes([68])+struct.pack('<I',(cl.va+REC)|2)
r+=bytes.fromhex('31 23 00000000 1b 000000')+bytes.fromhex('2702')*(lines-1)+bytes([0xe3])
r+=pack('Store Multi-Sample Resolved Tile Color Buffer (extended)',disable_color_buffer_write=255,disable_color_buffer_s_clear_on_write=1,disable_z_buffer_clear_on_write=1,disable_stencil_buffer_clear_on_write=1)
r+=pack('End of rendering')
cl.put(RCL,r+bytes(32))
# The vendor's compute shader record: 36-byte header, two 12-byte attributes.
rec=struct.pack('<9I',0x80,0x10000,0,(cl.va+FS)|4,cl.va+UNIF,0,0,0,0)
rec+=struct.pack('<3I',cl.va+VERTS,0x2096,4)
rec+=struct.pack('<3I',cl.va+ZW,0x209a,0)
cl.put(REC,rec)
fs=hexwords('3d003186bb800000 3c003186bb800000 3e203186bb800000 3c003186bb800000 3c003186bb800000 3c003186bb800000 3c003186bb800000 3c001006bbec0000 3e003186bb800000 3c003186bb800000 3c003186bb800000 3c003186bb800000 3c003186bb800000 3c001046bbec0000 3e003186bb800000 3c003186bb800000 3c003186bb800000 3c003186bb800000 3c003186bb800000 3c001086bbec0000 3e003186bb800000 3c003186bb800000 3c003186bb800000 3c003186bb800000 3c003186bb800000 3c0010c6bbec0000 3c003186bb800000 3c003186bb800000 3c403186bb800000 3c0031803c82e040 3de031807c838007 3c00318038806000 3de031807c838004 3c403186bb800000 3c00318038828000 3c0032c6bbf80000 3c0032c6bbf80040 3c0032c6bbf80080 3c0032c6bbf800c0 3c003346bbe00000 3c003186bb815000 3c203186bb800000 3c003186bb800000 3c003186bb800000')
cl.put(FS,struct.pack('<%dQ'%len(fs),*fs));cl.put(UNIF,struct.pack('<4I',0xffffff7b,0x1000,output.va+4096,0xfffffffc))
vertices=[]
for k in range(lines):vertices.extend([128,256+512*k,64*256+128,256+512*k])
cl.put(VERTS,struct.pack('<'+'H'*len(vertices),*vertices));cl.put(ZW,struct.pack('<2I',0,0x3f800000))
# CPU tiled ID image, two packed16-bit pairs per invocation.
def offset(x,y):
 return ((x&3)<<3)+((y&1)<<5)+(((x>>2)&1)<<6)+(((y>>1)&1)<<7)+(((((x>>3)>>2)*32)+((x>>3)&3)+(y>>2)*4)<<8)
def ids(x,y):return ((y*64+x)%128,0x1000+(y*64+x)//128,0x1234+x,0x2345+y)
source.put(0,bytes(source.size));color.put(0,b'\xa5'*color.size)
for y in range(32):
 for x in range(64):struct.pack_into('<4H',source.m,offset(x,y),*ids(x,y))
output.put(0,b'\xa5'*output.size)
handles=struct.pack('<4I',*[b.handle for b in bos]);hbuf=mmap.mmap(-1,4096);hbuf[:len(handles)]=handles
hptr=ctypes.addressof(ctypes.c_char.from_buffer(hbuf))
def stats():
    rows=open('/sys/bus/platform/devices/f1200000.gpu/gpu_stats').read().splitlines()[1:]
    return {r.split()[0]:int(r.split()[2]) for r in rows}
before=stats()
start=time.monotonic()
print('submit render-only, lines',lines,'RCL bytes',len(r),flush=True)
ioctl(SUBMIT_CL,'<10IQ4IQ',0,0,cl.va+RCL,cl.va+len(r),0,0,0,0,0,0,hptr,len(bos),1,0,0,0)
ioctl(WAIT_BO,'<IIQ',output.handle,0,5*10**9)
after=stats()
assert after['render']==before['render']+1,('render completion missing',before,after)
assert after['bin']==before['bin'],('unexpected bin job',before,after)
assert after['cache_clean']==before['cache_clean']+1,('missing cache clean',before,after)
print('completed one render job, no bin job, seconds',time.monotonic()-start,flush=True)
errors=0
for y in range(32):
 for x in range(64):
  i=y*64+x;got=struct.unpack_from('<4I',output.m,4096+i*16)
  expected=ids(x,y) if y<lines*2 else (0xa5a5a5a5,)*4
  if got!=expected:
   if errors<12:print('store mismatch',x,y,got,expected)
   errors+=1
assert output.m[:4096]==b'\xa5'*4096 and output.m[4096+32768:]==b'\xa5'*4096,'guard overwritten'
assert color.m[:]==b'\xa5'*color.size,'unexpected TLB output'
print('NV ID to vector TMU stores',lines*128,'invocations, errors',errors,flush=True)
assert errors==0
