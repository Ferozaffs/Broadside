"""Compare the generation settings of chosen cast portraits with the new shipwright ones (2026-11-22: the user says the
new ones look very different in art style from Dread Kate)."""
import json, os
os.chdir(os.path.dirname(os.path.abspath(__file__)))
want = {("dread_kate", 154330924), ("dread_widow", 551535185), ("frostholm_wright", 803926554),
        ("frostholm_wright", 272979778), ("saltmarsh_wright", 1728549410), ("saltmarsh_wright", 556941211)}
for ln in open("generation_log.jsonl", encoding="utf-8", errors="replace"):
    try:
        e = json.loads(ln)
    except ValueError:
        continue
    if (e.get("name"), e.get("seed")) in want:
        keys = {k: v for k, v in e.items() if k not in ("character", "files", "errors")}
        so = e.get("style_text_override")
        keys["style_text_override"] = (so[:300] + "...") if isinstance(so, str) else so
        print(json.dumps(keys, ensure_ascii=False)[:900])
        print()
