# mk_edsk.py
# 
# Generate an interesting CPC Extended DSK image.
# 
# Written & released by Keir Fraser <keir.xen@gmail.com>
# 
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import struct, sys

def main(argv):
    if len(argv) != 2:
        print("%s <output_file>" % argv[0])
        return
    nr_cyls = 40
    nr_heads = 2
    out_dat = bytearray()
    out_dat += struct.pack("48s",
                           b"EXTENDED CPC DSK File\r\nDisk-Info\r\nFF_Test")
    # nr_cyls, nr_heads, non_edsk_track_sz
    out_dat += struct.pack("<BBH", nr_cyls, nr_heads, 0)
    for i in range(nr_cyls * nr_heads):
        # track_sz // 256
        out_dat += struct.pack("B", (8192+256)//256)
    out_dat += bytearray(256-len(out_dat))
    for i in range(nr_cyls * nr_heads):
        n = (i%5) + 2 # 2-6
        sec_sz = 128 << n
        nr_sec = 8192 // sec_sz
        out_dat += struct.pack("16s", b"Track-Info\r\n")
        # cyl, head, pad[2], n, nr_secs, gap3, fill_byte
        out_dat += struct.pack("8B", i//nr_heads, i&nr_heads, 0, 0,
                               n, nr_sec, 84, 0xe5)
        for j in range(nr_sec):
            # c, h, r, n, stat1, stat2, actual_size
            out_dat += struct.pack("<6BH", i//nr_heads, i&nr_heads,
                                   j+3, n, 0, 0, sec_sz)
        out_dat += bytearray(256-len(out_dat)%256)
        out_dat += bytearray(8192)
    out_f = open(argv[1], "wb")
    out_f.write(out_dat)
    
if __name__ == "__main__":
    main(sys.argv)
