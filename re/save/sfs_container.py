"""
Starfield .sfs (BCPS) save container parser.

BCPS = "Bethesda Compressed Plugin Save". Layout (Starfield 1.x):

  offset  size  field
  0x00    4     magic "BCPS"
  0x04    4     u32   version (=1)
  0x08    8     u64   chunkHeaderSize (=0x48): size of the chunk-table header block that
                      starts at 0x40 (i.e. the bytes from 0x40 .. 0x40+chunkHeaderSize).
  0x10    8     u64   ? (0)
  0x18    8     u64   uncompressedHeaderRegion? / padding hint (0xA0 seen) -> actually the
                      offset where the compressed data begins (== 0x40 + chunkHeaderSize + 0x18).
                      We do not rely on this; we compute the data start from the chunk table.
  0x20    8     u64   total compressed payload size (== file_size - dataStart), e.g. 0x54C844
  0x28    8     u64   ? flags (0x40000000)
  0x30    8     u64   ? (0x40000)
  0x38    8     u64   chunkSize granularity (0x10 = 16? actually number-ish) -- see below

  Chunk table block (starts at 0x40, length = chunkHeaderSize bytes, here 0x48 = 72 bytes):
  0x40    4     u32   ? (0x9f371f03) -- looks like an id/seed, NOT a size
  0x44    4     char  compressor tag, "ZIP " (zlib) or "LZ4 "
  0x48    4     u32   uncompressedSize of (each?) chunk: 0x033e99
  0x4C    4     u32   compressedSize of first chunk: 0x019f05
  0x50..        u32[] further per-chunk compressed sizes (the file is split into fixed-size
                      uncompressed chunks, each zlib-compressed independently and concatenated)

The compressed payload (concatenated zlib streams) begins right after the chunk-table block,
i.e. at dataStart = 0x40 + chunkHeaderSize. We decompress every chunk and concatenate to get
the *decompressed save body*, which is the SkyrimSE/FO4-style save proper.

This module ONLY handles the container. The decompressed body is parsed by sfs_body.py.
"""

import struct
import sys
import zlib


class BCPSContainer:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.raw = f.read()
        self.path = path
        self._parse_header()

    def _parse_header(self):
        r = self.raw
        if r[:4] != b"BCPS":
            raise ValueError(f"not a BCPS save (magic={r[:4]!r})")
        self.version = struct.unpack_from("<I", r, 0x04)[0]
        self.chunk_header_size = struct.unpack_from("<Q", r, 0x08)[0]
        # header preamble words at 0x10..0x40
        self.hdr_words = struct.unpack_from("<6Q", r, 0x10)
        # EMPIRICAL: hdr_words[1] (file offset 0x18) is the offset where the compressed
        # payload begins (0xa0 in observed saves), and hdr_words[2] (offset 0x20) is the
        # TOTAL UNCOMPRESSED body size (verified == sum of all decompressed chunks).
        self.data_start = self.hdr_words[1]
        self.uncompressed_total = self.hdr_words[2]

        # chunk table block at 0x40: id u32, compressor 4cc, then [unc_chunk_size, comp0, ...]
        ct = 0x40
        self.chunk_table_off = ct
        self.chunk_id = struct.unpack_from("<I", r, ct + 0x00)[0]
        self.compressor = r[ct + 0x04: ct + 0x08]
        rest_off = ct + 0x08
        rest_end = self.data_start  # table runs right up to the payload start
        n_u32 = (rest_end - rest_off) // 4
        self.chunk_words = list(struct.unpack_from("<%dI" % n_u32, r, rest_off))

    def decompress(self):
        """Return the decompressed save body (bytes)."""
        if self.compressor.rstrip(b" \x00") == b"ZIP":
            return self._decompress_zip()
        elif self.compressor.rstrip(b" \x00") == b"LZ4":
            return self._decompress_lz4()
        else:
            raise ValueError(f"unknown compressor {self.compressor!r}")

    _ZLIB_HDR2 = (0x01, 0x5E, 0x9C, 0xDA)  # 0x78 0x?? valid zlib FLEVEL bytes

    def _decompress_zip(self):
        r = self.raw
        # Concatenated independent zlib streams starting at data_start. We decompress one
        # stream at a time; unused_data gives where the next begins. Any inter-stream
        # padding is skipped by seeking to the next valid zlib header (0x78 0x{01,5e,9c,da}).
        out = bytearray()
        pos = self.data_start
        end = len(r)
        chunk_count = 0
        while pos < end:
            do = zlib.decompressobj()
            try:
                chunk = do.decompress(r[pos:])
                chunk += do.flush()
            except zlib.error:
                if chunk_count == 0:
                    raise
                break
            out += chunk
            consumed = (end - pos) - len(do.unused_data)
            if consumed <= 0:
                break
            pos += consumed
            chunk_count += 1
            while pos < end - 1 and not (
                r[pos] == 0x78 and r[pos + 1] in self._ZLIB_HDR2
            ):
                pos += 1
        self.chunk_count = chunk_count
        if len(out) != self.uncompressed_total:
            sys.stderr.write(
                f"WARNING: decompressed {len(out)} != header uncompressed_total "
                f"{self.uncompressed_total}\n"
            )
        return bytes(out)

    def _decompress_lz4(self):
        import lz4.block  # only if needed
        raise NotImplementedError("LZ4 path not needed for these saves (compressor=ZIP)")

    def summary(self):
        return {
            "path": self.path,
            "version": self.version,
            "chunk_header_size": self.chunk_header_size,
            "data_start": hex(self.data_start),
            "compressor": self.compressor.decode("latin1"),
            "chunk_id": hex(self.chunk_id),
            "chunk_words[:8]": [hex(w) for w in self.chunk_words[:8]],
            "hdr_words": [hex(w) for w in self.hdr_words],
            "file_size": len(self.raw),
        }


if __name__ == "__main__":
    path = sys.argv[1]
    c = BCPSContainer(path)
    import json
    print(json.dumps(c.summary(), indent=2))
    body = c.decompress()
    print(f"decompressed body: {len(body):,} bytes from {len(c.raw):,} compressed; "
          f"chunks={c.chunk_count}")
    print("body[:32] hex:", body[:32].hex(" "))
    print("body[:16] ascii:", body[:16])
    if len(sys.argv) > 2:
        with open(sys.argv[2], "wb") as f:
            f.write(body)
        print("wrote body to", sys.argv[2])
