"""Convert an ESP32-S3 app .bin into a .uf2 for the MatrixPortal S3's drag-and-drop (TinyUF2) bootloader.

Usage: python tools/bin2uf2.py firmware.bin firmware.uf2

TinyUF2 on ESP32 expects addresses relative to the start of the app partition (base 0x0)
and the ESP32-S3 family ID. Only the app is written; the bootloader, settings and the
video storage partition are left alone.
"""
import struct
import sys

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
FLAG_FAMILY_ID_PRESENT = 0x00002000
ESP32S3_FAMILY_ID = 0xC47E5767
PAYLOAD = 256


def convert(data: bytes, base: int = 0x0) -> bytes:
    num_blocks = (len(data) + PAYLOAD - 1) // PAYLOAD
    out = bytearray()
    for i in range(num_blocks):
        chunk = data[i * PAYLOAD:(i + 1) * PAYLOAD]
        header = struct.pack(
            "<IIIIIIII",
            UF2_MAGIC_START0, UF2_MAGIC_START1, FLAG_FAMILY_ID_PRESENT,
            base + i * PAYLOAD, PAYLOAD, i, num_blocks, ESP32S3_FAMILY_ID,
        )
        body = chunk.ljust(476, b"\x00")
        out += header + body + struct.pack("<I", UF2_MAGIC_END)
    return bytes(out)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src = open(sys.argv[1], "rb").read()
    if src[:1] != b"\xe9":
        sys.exit("Input doesn't look like an ESP32 app image (missing 0xE9 header byte).")
    open(sys.argv[2], "wb").write(convert(src))
    print(f"Wrote {sys.argv[2]} ({len(src)} bytes of firmware)")
