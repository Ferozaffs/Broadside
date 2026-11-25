"""The sea's second cast (2026-11-22): the user met the same faces too often in the campaign (the castaway with the
coconut flask and bandana, the half-masked pirate) - more faces for the pools that come up most: pirates, castaways,
captives, merchants, navy, smugglers. Waits for the cast idle run (cast_idles2.log 'all done') so it never competes
with it; each render goes through comfy_guard (an empty queue first, restarts survived). Two seeds each.
Framing as the cast: thigh-up, anything held raised in a fist near the shoulder (pulling back to the knees loses the ink look)."""
import os, sys, time
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)
sys.path.insert(0, ROOT)
import comfy_guard

NEW = [
 # pirates (the most met: six more)
 ("pirate_7", "The character is a wiry Malay pirate woman in her thirties with a shaved head, intricate black tattoos down her neck and arms and a thin scar through one eyebrow. She wears a cropped dark red vest, a wide black sash stuffed with throwing knives and loose black trousers, and holds a curved kris raised in her right fist by her shoulder. A cold, amused stare."),
 ("pirate_8", "The character is an enormous bearded Norse pirate in his forties with a braided red-grey beard, a bald head painted with blue woad and a missing ear. He wears a sleeveless bearskin over a rusted mail shirt, iron arm rings, and holds a short boarding axe raised in his right fist beside his shoulder. He roars with laughter."),
 ("pirate_9", "The character is a handsome young French pirate in his twenties with long wavy chestnut hair, a thin moustache, a beauty mark and a single pearl earring. He wears an unbuttoned lace-cuffed shirt under a faded lilac brocade coat and holds a rapier raised upright in his right fist by his shoulder. A charming, dangerous smile."),
 ("pirate_10", "The character is a tall Black Caribbean pirate quartermistress in her thirties with long locs tied up in a crimson headwrap, gold rings in both ears and a gold tooth. She wears a sleeveless black leather bodice, a bandolier of pistols across her chest and a short cutlass raised in her right fist by her shoulder. Fierce, proud, grinning."),
 ("pirate_11", "The character is a grizzled old pirate gunner in his sixties with a wild grey beard singed at the ends, soot on his face, one eye clouded white and a peg-leg. He wears a powder-burnt leather apron over a striped shirt and holds a smoking linstock raised in his right fist by his shoulder. Half mad, delighted."),
 ("pirate_12", "The character is a pale, beautiful pirate woman in her twenties with jet black hair in a long braid, dark lipstick, kohl-rimmed green eyes and a silver nose ring. She wears a black tricorn with a raven feather, a tight black corset under a long tattered coat, and holds a flintlock pistol raised in her right fist by her shoulder. A sly, wicked smile."),
 # castaways (each with a past the passengers' talk tells)
 ("castaway_6", "The character is a castaway navy midshipman, a boy of about eighteen, sunburnt and thin, with matted fair hair and a wispy beard. He wears the shredded remains of a blue naval jacket with one gold button left, no shirt beneath, and clutches a bent brass spyglass to his chest. Shaken, hopeful eyes."),
 ("castaway_7", "The character is a castaway whaler woman in her forties, broad-shouldered and weathered, with cropped iron-grey hair, frost-scarred cheeks and a fur-lined hood pushed back. She wears a patched oilskin coat and holds a broken harpoon shaft raised in her right fist by her shoulder. Stubborn, unbowed."),
 ("castaway_8", "The character is a castaway ship's cook in his fifties, round-faced and bald with a huge black moustache, his apron torn and stained with salt. He wears a ragged white shirt and holds a dented iron ladle raised in his right fist by his shoulder like a weapon. Exhausted but laughing."),
 ("castaway_9", "The character is a beautiful castaway woman in her twenties with long sun-streaked copper hair full of salt, freckled tanned skin and grey eyes. She wears a torn and faded green silk gown cut short at the knee, bare shoulders, a gold locket, and holds a driftwood spear raised in her right fist by her shoulder. Wary, defiant, relieved."),
 # captives
 ("captive_f_6", "The character is a young noblewoman captive in her twenties with dark ringlets coming loose, flushed cheeks and fierce brown eyes, a bruise on one cheekbone. She wears a torn crimson silk dress with a laced bodice and a rope still tied round one wrist, which she raises in a fist by her shoulder. Furious, unafraid."),
 ("captive_f_7", "The character is a Chinese navigator captive in her thirties with a sleek black bob, sharp features and ink-stained fingers. She wears a dark blue quilted jacket, a jade pendant, and holds a rolled sea chart raised in her right fist by her shoulder. Calm, calculating, quietly amused."),
 ("captive_m_3", "The character is a captive young ship's surgeon in his twenties with tousled black hair, round spectacles with one cracked lens and a gentle face. He wears a blood-flecked white shirt with rolled sleeves and a leather waistcoat and holds a small leather medical bag raised in his right fist by his shoulder. Earnest, nervous, kind."),
 ("captive_m_4", "The character is a captive old harbour pilot in his sixties, a stocky Basque man with a white beard, thick eyebrows and a red beret. He wears a heavy wool sweater and a sailor's coat with the buttons torn off, and holds a knotted sounding line raised in his right fist by his shoulder. Gruff, grateful."),
 # merchants
 ("merchant_6", "The character is a glamorous Venetian merchant captain woman in her thirties with honey-blonde hair pinned up under a small plumed hat, a beauty mark and pearls at her throat. She wears a rich emerald brocade coat over a low-cut cream blouse and holds a fan of banknotes raised in her right fist by her shoulder. Shrewd, flirtatious."),
 ("merchant_7", "The character is a towering Ottoman merchant captain in his fifties with a magnificent black beard shot with grey, a white turban and kind dark eyes. He wears a long saffron kaftan with a gold-embroidered sash and holds a string of amber prayer beads raised in his right fist by his shoulder. Warm, dignified."),
 # navy
 ("navy_5", "The character is a dashing navy marine officer in his thirties with a strong jaw, short dark hair and a duelling scar on his chin. He wears a scarlet marine coat with white crossbelts and a black stock and holds a musket upright in his right fist by his shoulder. Steady, brave, a little reckless."),
 ("navy_6", "The character is a severe navy post-captain woman in her forties with steel-grey hair in a tight queue, a lean face and piercing pale eyes. She wears an immaculate dark blue coat with gold lace and a bicorne under her arm, and holds a telescope raised in her right fist by her shoulder. Cold, exacting, respected."),
 # smugglers
 ("smuggler_6", "The character is a roguish Cornish smuggler in his thirties with curly black hair, a gap-toothed grin and a gold tooth. He wears a knitted fisherman's sweater, a short pea coat and a knitted cap, and holds a small brandy cask raised on his right shoulder in his fist. Cheeky, untrustworthy, likeable."),
 ("smuggler_7", "The character is a beautiful Moroccan smuggler woman in her twenties with kohl-lined amber eyes, a nose chain and long dark hair under a loose indigo scarf. She wears a fitted indigo vest over a white shirt, a belt of small pouches and a curved dagger, and holds a silk-wrapped bundle raised in her right fist by her shoulder. Secretive, playful."),
]


def cast_done():
    try:
        return any(l.startswith("all done") for l in open("houses1.log", encoding="utf-8", errors="replace"))
    except OSError:
        return True


while not cast_done():
    print("waiting for the cast idle run ...", flush=True)
    time.sleep(300)
for cid, text in NEW:
    print(f"== {cid}", flush=True)
    comfy_guard.run([sys.executable, "-I", "tools/generate.py", "--style", "amadmir", "--name", cid, "--count", "2",
                     "--character", text])
print("all done", flush=True)
