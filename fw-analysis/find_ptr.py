import struct

data = open(r'C:\Temp\uwe\wcnmodem.bin', 'rb').read()
cnt = struct.unpack('<I', data[8:12])[0]
segs = []
for i in range(cnt):
    addr, foff, size = struct.unpack('<III', data[12+i*12:24+i*12])
    segs.append((addr, foff, size))

def virt_of(fileoff):
    for addr, foff, size in segs:
        if foff <= fileoff < foff + size:
            return addr + (fileoff - foff)

def file_of(virt):
    for addr, foff, size in segs:
        if addr <= virt < addr + size:
            return foff + (virt - addr)

targets = {0x40f0aa24: 'SNIFFER_MODE_SWITCH', 0x40f0aa41: 'SNIFFER_TRIGGER',
           0x40f0a52e: 'GET_INFO', 0x40f0873e: 'MAX1', 0x40f0aa5a: 'MAX2'}

for val, name in targets.items():
    pat = struct.pack('<I', val)
    i = data.find(pat)
    hits = []
    while i != -1:
        hits.append(i)
        i = data.find(pat, i+1)
    print(name, hex(val), 'hits:', [hex(h) for h in hits])

# dump around the first hit of SNIFFER_MODE_SWITCH pointer
pat = struct.pack('<I', 0x40f0aa24)
i = data.find(pat)
if i != -1:
    start = i - 64
    print('\n--- context around pointer at file 0x%x (virt 0x%x)' % (i, virt_of(i)))
    for k in range(start, start + 200, 16):
        chunk = data[k:k+16]
        words = ' '.join('%08x' % struct.unpack('<I', chunk[j:j+4])[0] for j in range(0, 16, 4))
        print('0x%06x  %s' % (k, words))
