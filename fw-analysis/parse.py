import struct, sys

data = open(r'C:\Temp\uwe\wcnmodem.bin', 'rb').read()
cnt = struct.unpack('<I', data[8:12])[0]
print('segments', cnt)

segs = []
for i in range(cnt):
    off = 12 + i*12
    addr, foff, size = struct.unpack('<III', data[off:off+12])
    segs.append((addr, foff, size))
    print('seg%d addr=0x%08x file=0x%06x size=0x%06x end=0x%06x' % (i, addr, foff, size, foff+size))

def virt_of(fileoff):
    for addr, foff, size in segs:
        if foff <= fileoff < foff + size:
            return addr + (fileoff - foff)
    return None

def file_of(virt):
    for addr, foff, size in segs:
        if addr <= virt < addr + size:
            return foff + (virt - addr)
    return None

for s in [b'WIFI_CMD_SNIFFER_MODE_SWITCH', b'WIFI_CMD_SNIFFER_TRIGGER', b'WIFI_CMD_GET_INFO', b'WIFI_CMD_MAX']:
    i = data.find(s)
    while i != -1:
        v = virt_of(i)
        print('%-32s file 0x%06x virt %s' % (s.decode(), i, ('0x%08x' % v) if v else None))
        i = data.find(s, i+1)
