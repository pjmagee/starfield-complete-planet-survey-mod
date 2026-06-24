#!/usr/bin/env python3
import struct, zlib
BA2 = r"E:/SteamLibrary/steamapps/common/Starfield/Data/Starfield - Misc.ba2"
with open(BA2,'rb') as f: data=f.read()
version = struct.unpack_from('<I', data, 4)[0]
btype = data[8:12]
file_count = struct.unpack_from('<I', data, 12)[0]
name_tbl_off = struct.unpack_from('<Q', data, 16)[0]
print("version",version,"type",btype,"count",file_count,"nameoff",hex(name_tbl_off))
# names
names=[]; p=name_tbl_off
for _ in range(min(file_count, 20)):
    ln=struct.unpack_from('<H',data,p)[0]; p+=2
    names.append(data[p:p+ln].decode('latin1','replace')); p+=ln
print("first names:", names[:12])
# Try to discover record size by checking that the name-table starts right after records.
# records start at 24. total record bytes = name_tbl_off - 24.
rec_area = name_tbl_off - 24
for rs in (32,36,40,48,24,28,30):
    if rec_area % rs == 0 and rec_area // rs == file_count:
        print("LIKELY rec_size =", rs, " (rec_area",rec_area,"/",rs,"=",rec_area//rs,")")
print("rec_area", rec_area, "rec_area/count =", rec_area/file_count)
# dump first 3 records at stride guess
for rs in (24,):
    pass
# Just hexdump first record region
print("first 80 bytes after header (off24):", data[24:24+80].hex())
