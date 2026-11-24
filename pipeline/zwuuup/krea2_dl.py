"""On ZWUUUP (2026-11-24, the user: "Lora is downloaded btw in downloads so you could download the other models and do a
test"): the Krea 2 Turbo base the Granblue LoRA needs, from Comfy-Org/Krea-2 on Hugging Face (public), into
K:\\ComfyUI-Models\\models. fp8_scaled diffusion model (the 4070 is Ada: fp8 yes, nvfp4 no) + fp8 Qwen3-VL 4B text
encoder + the Qwen-Image VAE, ~18.6 GB. curl.exe with resume (urllib hung on the xet CDN at 0 bytes); checks sizes.
The LoRA moves from Downloads (SHA256 1D0B0412...B224, matches Civitai) to loras\\granblue_fantasy_krea2.safetensors."""
import os, shutil, subprocess, sys, time
M = r"K:\ComfyUI-Models\models"
BASE = "https://huggingface.co/Comfy-Org/Krea-2/resolve/main/"
FILES = [("vae/qwen_image_vae.safetensors", "vae", 253806246),
         ("text_encoders/qwen3vl_4b_fp8_scaled.safetensors", "text_encoders", 5242467968),
         ("diffusion_models/krea2_turbo_fp8_scaled.safetensors", "diffusion_models", 13141730784)]

lora_src = os.path.expanduser(r"~\Downloads\GRANBLUEFANTASY-KREA-V1 (1).safetensors")
lora_dst = os.path.join(M, "loras", "granblue_fantasy_krea2.safetensors")
if os.path.exists(lora_src) and not os.path.exists(lora_dst):
    shutil.move(lora_src, lora_dst)
    print("moved LoRA ->", lora_dst, flush=True)

for rel, sub, size in FILES:
    dst = os.path.join(M, sub, os.path.basename(rel))
    if os.path.exists(dst) and os.path.getsize(dst) == size:
        print("have", dst, flush=True)
        continue
    part = dst + ".part"
    for attempt in range(30):
        if os.path.exists(part) and os.path.getsize(part) >= size:
            break
        t0, h0 = time.time(), os.path.getsize(part) if os.path.exists(part) else 0
        rc = subprocess.run(["curl.exe", "-sS", "-L", "--fail", "-C", "-", "--retry", "5", "--speed-limit", "100000",
                             "--speed-time", "60", "-o", part, BASE + rel]).returncode
        got = (os.path.getsize(part) if os.path.exists(part) else 0) - h0
        print(f"  {os.path.basename(rel)} curl rc {rc}: {os.path.getsize(part) / 1e9:.2f}/{size / 1e9:.2f} GB, "
              f"{got / 1e6 / max(1, time.time() - t0):.0f} MB/s", flush=True)
        if rc:
            time.sleep(15)
    if os.path.getsize(part) != size:
        sys.exit(f"FAILED {rel}: {os.path.getsize(part)} != {size}")
    os.replace(part, dst)
    print("done", dst, flush=True)
print("all done", flush=True)
