"""The memories' narrator (2026-11-23, the user: "Maybe we need a narrator"): a voice for the telling."""
import json, os
D = os.path.dirname(os.path.abspath(__file__))
v = json.load(open(os.path.join(D, "voices.json"), encoding="utf-8"))
v["narrator"] = ["f", "An older woman with a warm, low, weathered storyteller's voice, unhurried and gently wry, telling an old sea story by a fire; clear and intimate."]
json.dump(v, open(os.path.join(D, "voices.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print("ok")
