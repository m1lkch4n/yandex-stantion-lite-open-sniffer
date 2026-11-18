import struct
from capstone import *

data = open(r'C:\Temp\uwe\wcnmodem.bin', 'rb').read()
SEG0_BASE24 = 0xf00000
SEG0_FILE = 0x200

def s0(v):
    return SEG0_FILE + (v - SEG0_BASE24)

start, end = 0xa064, 0xa258
n = (end - start) // 4
print('table of', n, 'entries at file 0x%x (virt 0x40f%05x)' % (start, start - SEG0_FILE + 0x00000))

md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
md.detail = True

for k in range(n):
    v = struct.unpack('<I', data[start+k*4:start+k*4+4])[0]
    print('%3d 0x%02x -> 0x%08x' % (k, k, v | 0x40000000 if False else (0x40f00000 + (v - SEG0_BASE24) if v >= SEG0_BASE24 else v)))
