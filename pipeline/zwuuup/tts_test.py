"""Broadside's voices, first test (2026-11-22; the user: the synthesised babble "sounds like strange robot ... need to
run an ai model just like music"). Qwen3-TTS (Apache 2.0) from K:\\BroadsideTTS. The same characters and lines as
the babble sample, two ways:
  A  VoiceDesign per line: the character's voice description + the line's tone, every time
  B  VoiceDesign once (a reference line), then the Base model clones that voice for each line (one voice throughout)
The GPU is shared with ComfyUI: wait for its queue to be empty, pause this pipeline's own scripts (never the user's
jobs), make sure the queue is still empty, ask ComfyUI to unload its models, render, and resume the scripts whatever
happens. Run with K:\\BroadsideTTS\\venv\\Scripts\\python.exe."""
import ctypes, json, os, subprocess, sys, time
ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ROOT)
import comfy_guard

OUT = r"K:\BroadsideTTS\out\test1"
os.makedirs(os.path.join(OUT, "A"), exist_ok=True)
os.makedirs(os.path.join(OUT, "B"), exist_ok=True)

CAST = [
    ("haverly_tavern", "A big, jolly middle-aged English tavern keeper with a deep, booming, warm voice and a hearty laugh.",
     [("Come in, come in! Mind the dog. There's work on the board and ale in the barrel, and I'm not fussy which you take first.", "Cheerful and welcoming, loud.")]),
    ("saltmarsh_tavern", "A warm, flirtatious English barmaid in her thirties with a bright, husky voice and a working-class London accent.",
     [("Welcome to the Duck, love! Wipe your boots, grab a stool, and tell me something I haven't heard.", "Playful, teasing, warm.")]),
    ("haverly_wright", "A gruff old English shipwright in his seventies with a low, gravelly, rasping voice, slow and unimpressed.",
     [("Hmph. Built her sister, forty year back. Better timber then.", "Grumpy, dismissive, slow.")]),
    ("greywater_tavern", "A middle-aged widow who keeps a lonely tavern, with a low, quiet, breathy voice, eerie and melancholy.",
     [("The grey comes in at dusk. Folk stay indoors. You'll want to, too.", "Hushed, ominous, almost a whisper.")]),
    ("coral_tavern", "A huge Caribbean tavern keeper with a very deep, rich, rumbling bass voice and a big laugh.",
     [("Ha! Another captain with salt in the beard. Sit down, the rum's cold and the stories are hot.", "Laughing, boisterous, friendly.")]),
    ("dread_kate", "A wild young woman pirate captain with a fiery, sharp, raspy voice, manic and gleeful.",
     [("Smell that? That's your ship burning. Not yet. Soon.", "Menacing, gleeful, a little unhinged."),
      ("Burn, burn, burn! Ha!", "Shrieking with delighted laughter.")]),
    ("dread_corwin", "An elegant, aristocratic English gentleman pirate in his forties with a smooth, refined, drawling voice.",
     [("Ah, a guest. How tiresome. I shall have to ruin a perfectly good coat with you.", "Bored, sardonic, condescending.")]),
    ("pirate_1", "A brutal pirate in his forties with a rough, loud, snarling voice.",
     [("Curse you! Curse you to the depths!", "Furious, shouting, desperate.")]),
    ("captive_f_1", "A young English noblewoman in her twenties with a clear, high, educated voice.",
     [("Please, Captain! Get me off this ship, I beg you!", "Terrified, pleading, breathless.")]),
    ("greywater_wright", "A burly Scottish shipwright in his fifties with a strong Scottish accent and a deep, warm, exasperated voice.",
     [("Och, what've ye done to her? Never mind. Never tell a shipwright. It only makes us weep.", "Exasperated, theatrical, then resigned.")]),
]
REF_TEXT = "Aye, I've seen a few ships come and go from this harbour. Some of them even came back in one piece."

PIPE_SCRIPTS = ("cast_idles2.py", "sea2.py", "story_cast.py", "make_idle.py", "generate.py", "reroll")


def our_scripts():
    """This pipeline's own python processes (to pause while the GPU is ours)."""
    ps = ("Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'python.exe' -and $_.CommandLine -match '%s' } | "
          "ForEach-Object { $_.ProcessId }") % "|".join(s.replace(".", "\\.") for s in PIPE_SCRIPTS)
    out = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True).stdout
    me = os.getpid()
    return [int(x) for x in out.split() if x.strip().isdigit() and int(x) != me]


def pause(pids, on):
    k32, nt = ctypes.WinDLL("kernel32"), ctypes.WinDLL("ntdll")
    for pid in pids:
        h = k32.OpenProcess(0x0800, False, pid)  # PROCESS_SUSPEND_RESUME
        if h:
            (nt.NtSuspendProcess if on else nt.NtResumeProcess)(h)
            k32.CloseHandle(h)


def take_gpu():
    while True:
        comfy_guard.wait_idle()
        pids = our_scripts()
        pause(pids, True)
        time.sleep(3)
        if comfy_guard.comfy_idle():
            comfy_guard.free_vram()
            time.sleep(5)
            return pids
        pause(pids, False)  # (something got in first: wait again)
        time.sleep(30)


def render():
    import torch, soundfile as sf
    from qwen_tts import Qwen3TTSModel
    t0 = time.time()
    vd = Qwen3TTSModel.from_pretrained("Qwen/Qwen3-TTS-12Hz-1.7B-VoiceDesign", device_map="cuda:0", dtype=torch.bfloat16)
    print(f"VoiceDesign loaded in {time.time() - t0:.0f} s, {torch.cuda.memory_allocated() / 2**30:.1f} GB", flush=True)
    refs = {}
    n = 0
    for cid, desc, lines in CAST:
        for text, tone in lines:
            t = time.time()
            wavs, sr = vd.generate_voice_design(text=text, language="English", instruct=f"{desc} {tone}")
            sf.write(os.path.join(OUT, "A", f"{n:02d}_{cid}.wav"), wavs[0], sr)
            print(f"A {n:02d} {cid}: {len(wavs[0]) / sr:.1f} s audio in {time.time() - t:.1f} s", flush=True)
            n += 1
        wavs, sr = vd.generate_voice_design(text=REF_TEXT, language="English", instruct=f"{desc} Natural, conversational.")
        refs[cid] = (wavs[0], sr)
        sf.write(os.path.join(OUT, "B", f"ref_{cid}.wav"), wavs[0], sr)
    del vd
    torch.cuda.empty_cache()
    base = Qwen3TTSModel.from_pretrained("Qwen/Qwen3-TTS-12Hz-1.7B-Base", device_map="cuda:0", dtype=torch.bfloat16)
    n = 0
    for cid, desc, lines in CAST:
        prompt = base.create_voice_clone_prompt(ref_audio=refs[cid], ref_text=REF_TEXT, x_vector_only_mode=False)
        for text, tone in lines:
            t = time.time()
            wavs, sr = base.generate_voice_clone(text=text, language="English", voice_clone_prompt=prompt)
            sf.write(os.path.join(OUT, "B", f"{n:02d}_{cid}.wav"), wavs[0], sr)
            print(f"B {n:02d} {cid}: {len(wavs[0]) / sr:.1f} s audio in {time.time() - t:.1f} s", flush=True)
            n += 1
    print(f"peak {torch.cuda.max_memory_allocated() / 2**30:.1f} GB", flush=True)


pids = take_gpu()
print(f"GPU ours; paused {len(pids)} of the pipeline's scripts", flush=True)
try:
    render()
finally:
    pause(pids, False)
    print("resumed; all done", flush=True)
