"""On ZWUUUP: castaway_7, castaway_8 and smuggler_6 idle from the full-width crops (cut_crops.py: their old crops cut
off part of what the game shows, so the idle wouldn't fit the portrait). late_idles.py hasn't started on them;
late_boxes.json gets the new image + face/mouth boxes. merchant_7's old crop held the whole region: stays (hand boxes)."""
import json, os
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
lb = json.load(open("late_boxes.json", encoding="utf-8"))
cb = json.load(open("cut_boxes.json", encoding="utf-8"))
for cid in ("castaway_7", "castaway_8", "smuggler_6"):
    lb[cid] = cb[cid]
    print(cid, lb[cid])
json.dump(lb, open("late_boxes.json", "w", encoding="utf-8"), indent=1)
