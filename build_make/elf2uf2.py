#!/usr/bin/env python3
"""
elf2uf2.py — Convert ELF file to UF2 format for RP2040

Reads ELF LOAD segments and converts only those within the
RP2040 flash address range (0x10000000-0x10200000).

Usage: python elf2uf2.py input.elf output.uf2
"""

import sys
import struct

# UF2 constants
UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30
UF2_FLAG_FAMILY  = 0x00002000
RP2040_FAMILY_ID = 0xE48BFF56

# RP2040 address ranges
FLASH_START = 0x10000000
FLASH_END   = 0x10200000
SRAM_START  = 0x20000000
SRAM_END    = 0x20042000

PAGE_SIZE = 256

# ELF constants
EI_NIDENT = 16
ET_EXEC = 2
EM_ARM = 40
PT_LOAD = 1
PF_X = 1
PF_W = 2
PF_R = 4


def read_elf(filename):
    """Read ELF file and return list of (addr, data) segments."""
    with open(filename, 'rb') as f:
        # ELF header
        e_ident = f.read(EI_NIDENT)
        if e_ident[:4] != b'\x7fELF':
            raise ValueError("Not an ELF file")
        if e_ident[4] != 1:  # EI_CLASS = ELFCLASS32
            raise ValueError("Not a 32-bit ELF")
        if e_ident[5] != 1:  # EI_DATA = ELFDATA2LSB
            raise ValueError("Not little-endian")

        (e_type, e_machine, e_version, e_entry, e_phoff, e_shoff,
         e_flags, e_ehsize, e_phentsize, e_phnum, e_shentsize,
         e_shnum, e_shstrndx) = struct.unpack('<HHIIIIIHHHHHH',
                                               f.read(36))

        if e_type != ET_EXEC:
            raise ValueError(f"Not an executable ELF (type={e_type})")

        # Read program headers
        segments = []
        for i in range(e_phnum):
            f.seek(e_phoff + i * e_phentsize)
            (p_type, p_offset, p_vaddr, p_paddr, p_filesz,
             p_memsz, p_flags, p_align) = struct.unpack('<IIIIIIII',
                                                         f.read(32))

            if p_type != PT_LOAD:
                continue
            if p_filesz == 0:
                continue

            # Read segment data
            f.seek(p_offset)
            data = f.read(p_filesz)

            # Use physical address for loading
            addr = p_paddr

            # Only include segments in flash range
            if FLASH_START <= addr < FLASH_END:
                segments.append((addr, data))
            elif SRAM_START <= addr < SRAM_END:
                # SRAM segments with LOAD content (e.g., .data initialized data)
                # These need to be in flash at their LMA (load address)
                # But p_paddr should already point to flash for AT() sections
                pass  # Already handled by linker's AT() directive

        return segments, e_entry


def segments_to_pages(segments):
    """Convert segments to aligned pages for UF2."""
    pages = {}

    for addr, data in segments:
        offset = 0
        while offset < len(data):
            page_addr = (addr + offset) & ~(PAGE_SIZE - 1)
            page_offset = (addr + offset) - page_addr
            chunk_size = min(PAGE_SIZE - page_offset, len(data) - offset)

            if page_addr not in pages:
                pages[page_addr] = bytearray(PAGE_SIZE)

            pages[page_addr][page_offset:page_offset + chunk_size] = \
                data[offset:offset + chunk_size]
            offset += chunk_size

    return pages


def write_uf2(filename, pages):
    """Write UF2 file from page dictionary."""
    sorted_addrs = sorted(pages.keys())
    numblocks = len(sorted_addrs)

    with open(filename, 'wb') as f:
        for blockno, addr in enumerate(sorted_addrs):
            data = bytes(pages[addr])
            assert len(data) == PAGE_SIZE

            # UF2 block header (32 bytes)
            header = struct.pack('<IIIIIIII',
                UF2_MAGIC_START0,
                UF2_MAGIC_START1,
                UF2_FLAG_FAMILY,
                addr,
                PAGE_SIZE,
                blockno,
                numblocks,
                RP2040_FAMILY_ID
            )

            # Padding: 512 - 32 (header) - 256 (data) - 4 (footer) = 220
            padding = b'\x00' * 220

            # Footer (4 bytes)
            footer = struct.pack('<I', UF2_MAGIC_END)

            block = header + data + padding + footer
            assert len(block) == 512
            f.write(block)

    return numblocks


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} input.elf output.uf2")
        sys.exit(1)

    elf_file = sys.argv[1]
    uf2_file = sys.argv[2]

    try:
        segments, entry = read_elf(elf_file)
    except Exception as e:
        print(f"Error reading ELF: {e}")
        sys.exit(1)

    if not segments:
        print("No loadable segments found in flash range")
        sys.exit(1)

    total_size = sum(len(d) for _, d in segments)
    print(f"ELF: {len(segments)} segments, {total_size} bytes, "
          f"entry=0x{entry:08X}")

    for addr, data in segments:
        print(f"  0x{addr:08X}..0x{addr + len(data) - 1:08X} "
              f"({len(data)} bytes)")

    pages = segments_to_pages(segments)
    numblocks = write_uf2(uf2_file, pages)

    print(f"UF2: {numblocks} blocks ({numblocks * 512} bytes)")


if __name__ == '__main__':
    main()
