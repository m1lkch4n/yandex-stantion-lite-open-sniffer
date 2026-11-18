import struct
data = open(r'C:\Temp\uwe\wcnmodem.bin', 'rb').read()

SEG0_ADDR, SEG0_FILE = 0x40f00000, 0x200

def foff(virt24):
    return SEG0_FILE + (virt24 - (SEG0_ADDR & 0xffffff)) if 0 <= virt24 - (SEG0_ADDR & 0xffffff) else None

def read_str(v24):
    # v24 is 24-bit flash address (0x00f0xxxx)
    fo = SEG0_FILE + (v24 - (SEG0_ADDR & 0xffffff))
    if not (0 <= fo < len(data)):
        return None
    end = data.find(b'\0', fo)
    return data[fo:end].decode('ascii', 'replace')

tbl = 0xa380
entries = []
while tbl + 8 <= len(data):
    val, cid = struct.unpack('<II', data[tbl:tbl+8])
    if not (0x00f00000 <= val < 0x01000000):
        break
    s = read_str(val)
    if s is None or not s:
        break
    entries.append((cid, s))
    tbl += 8
    if s.startswith('WIFI_CMD_MAX') or s.startswith('WIFI_EVENT_'):
        break

print('entries:', len(entries))
for cid, s in entries:
    print('  %3d 0x%02x  %s' % (cid, cid, s))

print()
print('cmd 62 0x3e ->', [s for c, s in entries if c == 62])
print('cmd 85 0x55 ->', [s for c, s in entries if c == 85])
print('cmd 87 0x57 ->', [s for c, s in entries if c == 87])
