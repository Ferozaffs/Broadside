"""Encrypt a .glb into the .pak the game loads (see ShipModel::LoadExternal / DecodeShipPak).

Release downloads ship packs instead of loose model files: the third-party models' licences (Fab Standard,
CGTrader Royalty Free, Mixamo) allow them only inside the game, protected from extraction.

    python tools/pack_ship.py assets/ship/ship.glb <out-dir>/assets/ship/ship.pak

Format BSPAK2: "BSPAK2\\0\\0", a 12-byte random nonce, then the .glb encrypted with ChaCha20 under the
32-byte key in assets/pak.key (made on first use; never committed). The build compiles the same key into the
game (CMakeLists.txt -> pak_key.h). Without a key file the game can still read the old BSPAK1 packs.
"""
import os
import struct
import sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KEY_PATH = os.path.join(ROOT, "assets", "pak.key")


def load_key():
    if not os.path.exists(KEY_PATH):
        os.makedirs(os.path.dirname(KEY_PATH), exist_ok=True)
        with open(KEY_PATH, "wb") as f:
            f.write(os.urandom(32))
        print(f"made a new key: {KEY_PATH} (rebuild the game so it can read packs made with it)")
    key = open(KEY_PATH, "rb").read()
    if len(key) != 32:
        sys.exit(f"{KEY_PATH} must be 32 bytes")
    return key


def chacha20_stream(key, nonce, n):
    """ChaCha20 (RFC 8439) keystream of n bytes from block counter 0, all blocks at once."""
    blocks = (n + 63) // 64
    k = np.frombuffer(key, dtype="<u4").astype(np.uint32)
    nn = np.frombuffer(nonce, dtype="<u4").astype(np.uint32)
    s = np.zeros((16, blocks), dtype=np.uint32)
    s[0], s[1], s[2], s[3] = 0x61707865, 0x3320646E, 0x79622D32, 0x6B206574
    for i in range(8):
        s[4 + i] = k[i]
    s[12] = np.arange(blocks, dtype=np.uint32)
    s[13], s[14], s[15] = nn[0], nn[1], nn[2]
    x = s.copy()

    def rotl(v, c):
        return (v << np.uint32(c)) | (v >> np.uint32(32 - c))

    def qr(a, b, c, d):
        x[a] += x[b]; x[d] ^= x[a]; x[d] = rotl(x[d], 16)
        x[c] += x[d]; x[b] ^= x[c]; x[b] = rotl(x[b], 12)
        x[a] += x[b]; x[d] ^= x[a]; x[d] = rotl(x[d], 8)
        x[c] += x[d]; x[b] ^= x[c]; x[b] = rotl(x[b], 7)

    with np.errstate(over="ignore"):
        for _ in range(10):
            qr(0, 4, 8, 12); qr(1, 5, 9, 13); qr(2, 6, 10, 14); qr(3, 7, 11, 15)
            qr(0, 5, 10, 15); qr(1, 6, 11, 12); qr(2, 7, 8, 13); qr(3, 4, 9, 14)
        x += s
    return x.T.astype("<u4").tobytes()[:n]


def pack(src, dst):
    data = open(src, "rb").read()
    if data[:4] != b"glTF":
        sys.exit(f"{src} is not a binary glTF (.glb)")
    key = load_key()
    nonce = os.urandom(12)
    ks = np.frombuffer(chacha20_stream(key, nonce, len(data)), dtype=np.uint8)
    enc = (np.frombuffer(data, dtype=np.uint8) ^ ks).tobytes()
    os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
    with open(dst, "wb") as f:
        f.write(b"BSPAK2\0\0" + nonce + enc)
    print(f"packed {src} ({len(data)} bytes) -> {dst}")


def selftest():
    # RFC 8439 2.4.2 test vector: key 00..1f, nonce 000000000000004a00000000, counter 1
    key = bytes(range(32))
    nonce = bytes.fromhex("000000000000004a00000000")
    ks = chacha20_stream(key, nonce, 128)[64:128]  # (block 1)
    pt = b"Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it."
    ct = bytes(a ^ b for a, b in zip(pt[:64], ks))
    assert ct.hex().startswith("6e2e359a2568f98041ba0728dd0d6981"), ct.hex()
    print("chacha20 ok")


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        selftest()
    elif len(sys.argv) != 3:
        sys.exit(__doc__)
    else:
        pack(sys.argv[1], sys.argv[2])
