import json
NEW = [
 ("smuggler_4", "A Smuggler", "Running contraband", "The character is a sharp-featured smuggler woman in her forties with black hair cut to the jaw, a thin white scar on her chin and dark watchful eyes. She wears a long weathered brown greatcoat with deep pockets over a grey waistcoat, fingerless gloves, and has a coil of rope over one shoulder and a small keg under her arm. Calm, unreadable, quietly dangerous."),
 ("smuggler_5", "A Smuggler", "Running contraband", "The character is a cheerful plump smuggler woman in her fifties with rosy cheeks, curly grey-blonde hair under a red headscarf and a gold tooth in her grin. She wears a patched blue apron over a striped skirt and a heavy shawl, a basket of 'fish' on her hip with bottles poking out. Innocent face, wicked eyes."),
 ("castaway_4", "A Castaway", "Taken off the beach", "The character is a sturdy castaway woman in her forties with greying brown hair hacked short, a sunburnt weathered face and strong arms. She wears a torn and faded red naval jacket with no shirt beneath, wrapped with a sailcloth belt, a long knife made from a barrel hoop and a spyglass with a cracked lens. Grim, capable, hugely relieved."),
 ("castaway_5", "A Castaway", "Taken off the beach", "The character is a young castaway girl in her late teens with long wild dark curly hair full of salt, freckled brown skin and wide amazed eyes. She wears a ragged yellowed linen dress cut short at the knee, a necklace of shells and shark teeth, and holds a woven palm-leaf hat. Shy, grateful, curious."),
 ("captive_f_4", "Freed Captive", "Freed from the pirates", "The character is an elegant older lady in her sixties with silver hair pinned up, a long dignified face and sharp blue eyes behind a lorgnette. She wears a rich plum-coloured travelling gown, slightly torn at the hem, a cameo brooch and a lace shawl, holding a closed parasol like a weapon. Haughty, unshaken, very unimpressed with pirates."),
 ("captive_f_5", "Freed Captive", "Freed from the pirates", "The character is a young East Asian woman in her twenties, a ship's surgeon, with straight black hair tied back, a calm intelligent face and a smudge of soot on her cheek. She wears a practical dark grey dress with rolled sleeves and a leather apron, a satchel of medical instruments over her shoulder. Composed, kind, resolute."),
 ("navy_4", "Commander Isobel Thorne", "Commander of a navy brig", "The character is a tall naval commander woman in her forties with dark auburn hair in a tight queue under a black bicorne hat, a strong nose and grey eyes. She wears a dark blue naval coat with white lapels and gold buttons, a gold epaulette on one shoulder, a sword belt and a brass telescope tucked under one arm. Stern, fair, utterly in command."),
 ("merchant_5", "Captain Hester Lowe", "Master of a merchantman", "The character is a wiry elderly merchant captain woman in her sixties with white hair in a long plait, a deeply lined tanned face and a clay pipe. She wears a faded green coat with brass buttons over a knitted jumper, canvas trousers and sea boots, a battered leather ledger under her arm. Shrewd, salty, kind."),
]
cast = json.load(open("cast.json", encoding="utf-8"))
have = {c["id"] for c in cast}
for cid, name, title, ch in NEW:
    if cid not in have:
        cast.append({"id": cid, "name": name, "title": title, "character": ch})
json.dump(cast, open("cast.json", "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print(len(cast), "cast")
