"""Read the current DRM framebuffer via GETFB2/MAP_DUMB, without taking master."""
import collections,fcntl,json,mmap,os,re,struct
from pathlib import Path

def capture(out):
 state=Path('/sys/kernel/debug/dri/1/state').read_text()
 fb=int(re.search(r'\n\s*fb=(\d+)',state).group(1))
 fd=os.open('/dev/dri/card1',os.O_RDWR|os.O_CLOEXEC)
 try:
  cmd=bytearray(104);struct.pack_into('<I',cmd,0,fb)
  fcntl.ioctl(fd,0xc06864ce,cmd,True)
  fields=struct.unpack('<17I4x4Q',cmd)
  _,width,height,fourcc,_=fields[:5];handle=fields[5];pitch=fields[9];offset=fields[13]
  assert handle and fourcc==int.from_bytes(b'AR24','little'),fields
  try:
   mapping=bytearray(struct.pack('<IIQ',handle,0,0))
   fcntl.ioctl(fd,0xc01064b3,mapping,True)
   address=struct.unpack('<IIQ',mapping)[2]
   with mmap.mmap(fd,offset+pitch*height,flags=mmap.MAP_SHARED,prot=mmap.PROT_READ,offset=address) as mem:
    data=mem[offset:offset+pitch*height]
   (out/'scanout.bgra').write_bytes(data)
   alpha=collections.Counter(data[3::4]);assert alpha=={255:width*height},alpha
   for row,y in enumerate((87,71,55,39)):
    for i in range(256):
     expected=bytes([i if row in (0,3) else 0,i if row in (0,2) else 0,i if row in (0,1) else 0,255])
     actual=data[y*pitch+(32+i)*4:y*pitch+(33+i)*4]
     assert actual==expected,(row,i,list(actual),list(expected))
   result=dict(width=width,height=height,pitch=pitch,format='ARGB8888',alpha_counts=dict(alpha),ramp_pixels_checked=1024)
   (out/'scanout.json').write_text(json.dumps(result,indent=2)+'\n')
   (out/'drm-state.txt').write_text(state)
   return result
  finally:fcntl.ioctl(fd,0x40086409,struct.pack('<II',handle,0))
 finally:os.close(fd)
