"""Scramble a ship .glb into the ship.pak the game loads (see ShipModel::LoadExternal).

Release downloads ship the pack instead of the loose .glb, because the third-party ship model's
licence does not allow handing the model out on its own.

    python tools/pack_ship.py assets/ship/ship.glb <out-dir>/assets/ship/ship.pak
"""
import os
import struct
import sys


def pack(src, dst):
    data = open(src, "rb").read()
    if data[:4] != b"glTF":
        sys.exit(f"{src} is not a binary glTF (.glb)")
    seed = struct.unpack("<I", os.urandom(4))[0] or 0x9E3779B9
    x = seed
    out = bytearray(len(data))
    for i, b in enumerate(data):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        out[i] = b ^ (x >> 24) ^ ((i * 131) & 0xFF)
    os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
    with open(dst, "wb") as f:
        f.write(b"BSPAK1\0\0" + struct.pack("<I", seed) + bytes(out))
    print(f"packed {src} ({len(data)} bytes) -> {dst}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    pack(sys.argv[1], sys.argv[2])
