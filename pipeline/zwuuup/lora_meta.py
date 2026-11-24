"""On ZWUUUP: the Granblue LoRA the user downloaded - SHA256, safetensors metadata and a sample of its tensor names
(they tell the base model's architecture)."""
import hashlib, json, os, struct, sys
p = os.path.expanduser(r"~\Downloads\GRANBLUEFANTASY-KREA-V1 (1).safetensors")
h = hashlib.sha256()
with open(p, "rb") as f:
    for b in iter(lambda: f.read(1 << 20), b""):
        h.update(b)
print("SHA256", h.hexdigest().upper())
with open(p, "rb") as f:
    n = struct.unpack("<Q", f.read(8))[0]
    hdr = json.loads(f.read(n))
meta = hdr.pop("__metadata__", {})
for k, v in meta.items():
    print("META", k, str(v)[:300])
keys = sorted(hdr)
print("TENSORS", len(keys))
for k in keys[:12] + keys[-6:]:
    print(" ", k, hdr[k]["shape"], hdr[k]["dtype"])
