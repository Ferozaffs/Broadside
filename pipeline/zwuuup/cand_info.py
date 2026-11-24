"""On ZWUUUP: loop-check problems of the light-up candidates (clean colour, no flash)."""
import json, os
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
RUNS = ["captive_f_4_s915143245_w777_cfg1", "captive_f_4_s915143245_w4242_cfg1", "captive_f_4_s915143245_w1234_cfg1",
        "merchant_3_s992547329_w31337_cfg1", "merchant_3_s992547329_w8080_cfg1", "merchant_3_s992547329_w4242_cfg1",
        "fleet_1_s673841449_w1234_cfg1", "captive_f_5_s1391098847_w9001_cfg1"]
for r in RUNS:
    cid = r.split("_s")[0]
    p = f"assets/characters/{cid}/idle/{r}/idle_info.json"
    lc = json.load(open(p, encoding="utf-8")).get("loop_check", {})
    print(r, {k: lc.get(k) for k in ("seam_ratio", "max_drift_from_art", "median_step")}, lc.get("problems"))
