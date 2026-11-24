"""Every game portrait's character text (the Amadmir prompt it was drawn from), for writing the anime pack's prompts:
cast.json, then the later scripts' NEW lists / TEXT (later ones win) -> cast_texts.json {id: text}."""
import ast, json, os
S = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ZW = os.path.join(S, "zw")
out = {x["id"]: x["character"] for x in json.load(open(os.path.join(ZW, "cast.json"), encoding="utf-8"))}
for f in ("cast_more.py", "cast_more2.py", "cast_redo.py", "recast.py", "sea2.py", "story_cast.py", "wrights.py", "wrights2.py", "fleet7.py"):
    p = os.path.join(ZW, f)
    if not os.path.exists(p):
        continue
    tree = ast.parse(open(p, encoding="utf-8").read())
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name):
            try:
                v = ast.literal_eval(node.value)
            except Exception:
                continue
            name = node.targets[0].id
            if name == "TEXT" and isinstance(v, str) and f == "fleet7.py":
                out["fleet_7"] = v
            rows = v.items() if isinstance(v, dict) else v if isinstance(v, (list, tuple)) else []
            for r in rows:
                if isinstance(r, tuple) and r and isinstance(r[0], str):
                    txt = next((x for x in r[1:] if isinstance(x, str) and x.startswith("The character")), None)
                    if txt:
                        out[r[0]] = txt
                    elif isinstance(r[1], str) and r[1].startswith("The character"):
                        out[r[0]] = r[1]
ids = open(os.path.join(S, "portrait_ids.txt")).read().split()
missing = [i for i in ids if i not in out]
json.dump({i: out[i] for i in ids if i in out}, open(os.path.join(S, "pack", "cast_texts.json"), "w", encoding="utf-8"), indent=1)
print(len(ids) - len(missing), "of", len(ids), "missing:", missing)
