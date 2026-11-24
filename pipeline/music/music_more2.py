import json
INST = "[intro]\n\n[instrumental]\n\n[instrumental]\n\n[instrumental]\n\n[outro]"
TRACKS = [
 # the Ember Reach: fire, not the forge - warm and smouldering, light hand percussion
 ("sea_ember_b", 150, "instrumental, cinematic world music, volcanic sea at dusk, warm flamenco guitar melody, duduk, soft hand percussion, "
                      "low warm strings, gentle frame drum, 88 BPM, smouldering, glowing embers, warm, mysterious, soft drums, no vocals"),
 # the Fog Banks: beautiful melancholy, not dread
 ("sea_fog_b", 150, "instrumental, beautiful melancholy, misty sea at dawn, solo cello melody, soft piano, harp, gentle strings, distant bell, "
                    "70 BPM, wistful, tender, bittersweet, dreamy, no vocals"),
 # the Tropics: bachata and salsa
 ("sea_tropics_bachata", 150, "instrumental, bachata, romantic caribbean sea voyage, requinto guitar melody, rhythm guitar, bongos, guira, "
                              "electric bass, 126 BPM, warm, sunny, romantic, swaying, no vocals"),
 ("sea_tropics_salsa", 150, "instrumental, salsa, tropical sea voyage, piano montuno, trumpet and trombone section, congas, timbales, clave, "
                            "bass tumbao, 180 BPM, joyful, warm, lively, no vocals"),
]
tracks = json.load(open("tracks.json", encoding="utf-8"))
byname = {t["name"]: t for t in tracks}
for name, secs, style in TRACKS:
    if name in byname: byname[name].update(seconds=secs, style=style)
    else: tracks.append({"name": name, "seconds": secs, "style": style, "lyrics": INST})
json.dump(tracks, open("tracks.json", "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print(len(tracks), "tracks")
