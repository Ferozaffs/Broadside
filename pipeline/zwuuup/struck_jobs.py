"""The pirate faces' 'struck her colours' lines (2026-11-23: a quest's named pirate now gets one of the pirate faces,
one to go with her name): the directions the sea_pirate face's lines had in jobs_all.json."""
import json, os
D = os.path.dirname(os.path.abspath(__file__))
dirs = {line: d for face, h, line, d in json.load(open(os.path.join(D, "jobs_all.json"), encoding="utf-8")) if face == "sea_pirate"}
rows = json.load(open(os.path.join(D, "struck_rows.json"), encoding="utf-8"))
jobs = [[face, h, line, dirs.get(line, "A pirate captain who has just struck her colours to you, beaten, calling across the water. Sullen, defeated.")] for face, h, line, ctx in rows]
json.dump(jobs, open(os.path.join(D, "jobs_struck.json"), "w", encoding="utf-8"), ensure_ascii=False)
print(len(jobs), "jobs,", sum(1 for j in jobs if j[2] in dirs), "with sea_pirate's direction")
