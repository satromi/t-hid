#!/usr/bin/env python3
"""
bin2uf2.py — Convert binary file to UF2 format for RP2040
Usage: python3 bin2uf2.py input.bin output.uf2 [start_addr]
       Default start_addr = 0x10000000 (RP2040 XIP Flash)
"""
import sys
import struct

UF2_MAGIC_START0 = 0x0A324655  # "UF2\n"
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30
UF2_FLAG_FAMILY  = 0x00002000
RP2040_FAMILY_ID = 0xE48BFF56

def convert(infile, outfile, start_addr=0x10000000):
    with open(infile, 'rb') as f:
        indata = f.read()

    datapadding = b'\x00' * 476
    numblocks = (len(indata) + 255) // 256
    outp = b''

    for blockno in range(numblocks):
        ptr = 256 * blockno
        chunk = indata[ptr:ptr+256]
        flags = UF2_FLAG_FAMILY
        hd = struct.pack('<IIIIIIII',
            UF2_MAGIC_START0, UF2_MAGIC_START1,
            flags,
            ptr + start_addr,
            256,
            blockno,
            numblocks,
            RP2040_FAMILY_ID
        )
        while len(chunk) < 256:
            chunk += b'\x00'
        block = hd + chunk + datapadding[:476 - 256] + struct.pack('<I', UF2_MAGIC_END)
        assert len(block) == 512
        outp += block

    with open(outfile, 'wb') as f:
        f.write(outp)
    print(f"Converted {len(indata)} bytes to {len(outp)} bytes ({numblocks} blocks)")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} input.bin output.uf2 [start_addr]")
        sys.exit(1)
    addr = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x10000000
    convert(sys.argv[1], sys.argv[2], addr)
