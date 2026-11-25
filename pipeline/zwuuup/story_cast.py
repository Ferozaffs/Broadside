"""The story's people (2026-11-22, docs/campaign.md "The story"): Mara Vell (the first mate; later the Storm Caller),
Lord Admiral Caspian Ashgrave, and the Sovereign's five officers who come home one a region. After the sea's second
cast (sea2.log 'all done'); each render through comfy_guard. Framing as the cast: thigh-up, anything held raised in a
fist near the shoulder. Four seeds for the two leads, two for the officers."""
import os, sys, time
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
import comfy_guard

NEW = [
 ("story_mara", 4, "The character is a beautiful young woman first mate of a navy ship of the line, in her late twenties, with long storm-grey hair in a loose sailor's braid, sea-green eyes, sun-browned skin and a small silver lighthouse pendant. She wears a fitted dark blue naval officer's coat with tarnished gold buttons over a white shirt open at the throat, and holds a brass speaking trumpet raised in her right fist by her shoulder. A warm, fearless half smile."),
 ("story_mara_storm", 4, "The character is a beautiful young woman in her late twenties possessed by the sea, with long storm-grey hair whipping loose around her, pale skin, glowing sea-green eyes with no pupils, faint blue lightning crackling across her fingers and a small silver lighthouse pendant. She wears a torn dark blue naval officer's coat over a soaked white shirt, and raises her right fist by her shoulder with lightning around it. Sorrowful, distant, terrible."),
 ("story_ashgrave", 4, "The character is a tall, distinguished lord admiral in his late fifties with a lean aristocratic face, silver hair tied back with a black ribbon, a neat silver beard and cold grey eyes. He wears a magnificent black naval coat heavy with gold lace, epaulettes and orders, a black sash, and holds a broken bronze bell fragment raised in his right fist by his shoulder. Calm, charismatic, utterly certain."),
 ("officer_stroud", 2, "The character is a huge, bald navy bosun in his forties with a thick red beard, a broken nose, tattooed forearms and a bosun's silver whistle on a chain. He wears a sleeveless striped sailor's shirt and canvas trousers, and holds a coiled rope raised in his right fist by his shoulder. Loyal, gruff, grinning."),
 ("officer_fairweather", 2, "The character is a sharp young navy navigator woman in her twenties with short black curls, brown skin, freckles and brass-rimmed spectacles pushed up on her head. She wears a dark green coat with ink stains on the cuffs over a cream waistcoat, and holds a pair of brass dividers raised in her right fist by her shoulder. Clever, wry, determined."),
 ("officer_gage", 2, "The character is a grizzled navy master gunner in his fifties with a weathered face, close-cropped grey hair, powder burns on his cheek and frost-scarred hands. He wears a leather gunner's apron over a faded blue jacket and holds a smoking slow-match raised in his right fist by his shoulder. Steady, quiet, dangerous."),
 ("officer_whitlock", 2, "The character is a gentle navy ship's surgeon in his thirties, thin and pale with tired kind eyes, untidy brown hair and a soot-smudged face. He wears a rumpled black coat over a bloodstained white shirt with rolled sleeves and holds a small brass lantern raised in his right fist by his shoulder. Weary, brave, compassionate."),
 ("officer_penhallow", 2, "The character is a tough navy quartermaster woman in her forties with a strong jaw, cropped iron-grey hair, a scar through one eyebrow and silver rings in one ear. She wears a weathered blue pea coat over a grey wool sweater and holds a ship's ledger raised in her right fist by her shoulder. Unshakable, dry, fiercely loyal."),
]


def done(log):
    try:
        return any(l.startswith("all done") for l in open(log, encoding="utf-8", errors="replace"))
    except OSError:
        return False


while False:  # (was: after the cast idle run; now taking turns with it)
    print("waiting for the cast idle run ...", flush=True)
    time.sleep(300)
for cid, n, text in NEW:
    print(f"== {cid}", flush=True)
    comfy_guard.run([sys.executable, "-I", "tools/generate.py", "--style", "amadmir", "--name", cid, "--count", str(n),
                     "--character", text])
print("all done", flush=True)
