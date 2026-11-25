import json
# the ones kept as they were (the user's picks): their first prompt back, so a re-render gives the face she chose
KEEP = "haverly_tavern saltmarsh_wright greywater_wright coral_tavern iceward_wright ashfall_harbour mournwick_wright lantern_wright fleet_5 pirate_3 pirate_6 castaway_1 castaway_2".split()
cast = json.load(open("cast.json", encoding="utf-8"))
n = 0
for c in cast:
    if c["id"] in KEEP and "character_v1" in c:
        c["character_v2"] = c["character"]; c["character"] = c.pop("character_v1"); n += 1
json.dump(cast, open("cast.json", "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print(n, "restored")
