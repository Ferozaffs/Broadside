"""The anime pack's portrait prompts (Krea 2 Turbo + Granblue LoRA, krea2_run.py adds the style and framing). The
recipe the user rated 10/10 (anime_jobs2.py): every woman beautiful and alluring in Calypso's terms - full lips, a
fitted corseted bodice with cleavage, a hand on the hip or the prop held up, a knowing look; the older ones glamorous
mature women. Each keeps her game portrait's concept, signature prop and colours (cast_texts.json), and her face
carries her voice's archetype (anime/voices.tsv). Already picked (anime_test, anime_test2): dread_calypso, fleet_1,
navy_2, captive_f_4, haverly_harbour."""
P = {}

# ---- set 1: the story ----
P["story_mara"] = (
    "The character is a beautiful, warm-hearted young woman first mate in her twenties with long storm-grey hair in a "
    "loose sailor's braid over one shoulder, bright sea-green eyes, sun-kissed skin, full lips and a small silver "
    "lighthouse pendant at her throat. She wears a fitted dark blue naval officer's coat with tarnished gold buttons, "
    "open over a white shirt unbuttoned low and a laced brown leather corset that hugs her curvy figure, and holds a "
    "brass speaking trumpet in one hand with the other on her hip. She beams with a sweet, affectionate, open smile.")
P["story_mara_storm"] = (
    "The character is a beautiful, terrible young woman possessed by the sea, in her twenties, with long storm-grey hair "
    "whipping loose around her, pale skin, full dark lips and glowing sea-green eyes without pupils, a small silver "
    "lighthouse pendant at her throat and blue lightning crackling across her fingers. She wears a torn dark blue naval "
    "officer's coat falling off her shoulders over a soaked, clinging white shirt and a black corset, and raises one hand "
    "wreathed in lightning. Sorrowful, distant and dangerously alluring, a faint sad smile.")
P["story_ashgrave"] = (
    "The character is a tall, stunning lady admiral in her forties, a glamorous mature woman with long silver hair tied "
    "back with a black ribbon, a lean aristocratic face, cold grey eyes and full dark red lips. She wears a magnificent "
    "black naval coat heavy with gold lace, epaulettes and orders, open over a black silk corseted bodice with a plunging "
    "neckline, a black sash at her waist, and holds a broken bronze bell fragment raised in one gloved hand. Regal, "
    "imperious and utterly certain, looking down at the viewer with a cool, godlike smile.")
P["officer_stroud"] = (
    "The character is a tall, strong, athletic young woman bosun in her twenties with a messy mane of fiery red hair, "
    "freckles, a cheeky grin, full lips, tattooed toned arms and a silver bosun's whistle on a chain between her breasts. "
    "She wears a cropped sleeveless striped sailor's top that shows her midriff and cleavage and canvas trousers with a "
    "wide belt, and holds a coiled rope over one shoulder with her other fist on her hip. Loud, bouncy, full of energy, "
    "laughing.")

# ---- set 2: the officers ----
P["officer_fairweather"] = (
    "The character is a sharp, beautiful young navigator woman in her twenties with short glossy black curls, brown skin, "
    "freckles, full lips and brass-rimmed spectacles pushed up on her head. She wears a fitted dark green coat with ink "
    "stains on the cuffs, open over a cream laced corset waistcoat with a low neckline, and holds a pair of brass dividers "
    "raised in one hand with the other on her hip. Cool and composed, a calm, unreadable half-lidded gaze.")
P["officer_gage"] = (
    "The character is a stoic, beautiful woman master gunner in her thirties with a short silver-grey bob, a weathered "
    "but striking face, a powder burn on one cheek, full lips and steady dark eyes. She wears a leather gunner's apron "
    "over a fitted faded blue jacket open on a black corset with a low neckline, and holds a smoking slow-match raised in "
    "one gloved hand with the other on her hip. Quiet, cool and dangerous, a flat calm stare.")
P["officer_whitlock"] = (
    "The character is a gentle, shy and very pretty young woman ship's surgeon in her twenties with untidy soft brown "
    "hair, pale skin, tired kind eyes, a soot smudge on her cheek and full lips. She wears a rumpled black coat slipping "
    "off her shoulders over a white blouse with rolled sleeves and a fitted corset, and holds a small brass lantern close "
    "to her chest in both hands. Bashful and blushing, glancing up at the viewer with a timid little smile.")
P["officer_penhallow"] = (
    "The character is a tough, glamorous woman quartermaster in her thirties with cropped iron-grey hair, a strong jaw, a "
    "scar through one eyebrow, silver rings in one ear and full lips. She wears a weathered blue pea coat open over a "
    "tight grey ribbed sweater that hugs her curvy figure, and holds a ship's ledger against her hip. Mature and sure of "
    "herself, a teasing big-sister smirk.")

# ---- set 3: Haverly and Lantern Rock ----
P["haverly_wright"] = (
    "The character is a sharp-tongued, beautiful woman shipwright in her twenties with long ash-grey hair in a high "
    "ponytail, half-moon spectacles low on her nose, sharp amber eyes and full pouting lips. She wears a tar-stained "
    "ox-blood red coat open over a tight leather corset and a low-cut white blouse, a leather apron at her hips and brass "
    "calipers on a chain between her breasts, and holds a rolled ship's plan in one hand with the other on her hip. She "
    "squints over her spectacles with an annoyed, flustered pout, cheeks a little pink.")
P["haverly_tavern"] = (
    "The character is a buxom, jolly, gorgeous tavern keeper woman in her thirties with thick wavy ginger hair loosely "
    "pinned up, rosy cheeks, warm crinkled eyes and full red lips. She wears a mustard-yellow brocade corset bodice over "
    "a white off-the-shoulder blouse with a deep neckline, a burgundy ribbon at her throat and a clean leather apron, a "
    "ring of iron keys at her belt, and raises a foaming pewter tankard in a toast. A warm, motherly, indulgent laugh, "
    "everyone's favourite landlady.")
P["lantern_harbour"] = (
    "The character is an ageless, mysterious lighthouse keeper, a petite, slender adult woman with long silver-white "
    "hair, pale skin and ancient pale glowing eyes, her face half in the shadow of a deep grey oilskin hood. She wears a "
    "heavy grey oilskin coat with brass buttons over a fitted dark grey dress, a coil of rope at her waist, and holds a "
    "glowing brass lamp close to her chest. She looks at the viewer with an old, knowing, amused little smile.")
P["lantern_tavern"] = (
    "The character is a beautiful, pale, graceful tavern owner in her thirties with long dark hair pinned up in loose "
    "curls, dark red lips and a black lace mourning veil falling over one eye. She wears an elegant off-the-shoulder "
    "black velvet gown with a corseted waist and jet beads along the plunging neckline, a silver locket, and holds a "
    "glass of dark red wine. Melancholy, refined and alluring, a soft, polite, sorrowful smile.")

# ---- set 4: Lantern Rock and Saltmarsh ----
P["lantern_wright"] = (
    "The character is a gentle, voluptuous shipwright nun in her thirties with a soft round face, kind warm eyes, full "
    "lips and a few strands of chestnut hair escaping a simple brown veil, a wooden rosary wound round one wrist. She "
    "wears a fitted coarse brown wool habit with the sleeves tied back, laced tight at the waist over her curvy figure, "
    "a rope belt hung with tools, and holds a wooden mallet and chisel at her chest. A serene, motherly, indulgent smile.")
P["saltmarsh_harbour"] = (
    "The character is a shy, pretty young harbour clerk woman in her twenties with long straight black hair, a long pale "
    "face, ink-stained fingers, big nervous dark eyes and full lips. She wears a fitted black tailcoat over a white "
    "blouse and a black corset, a loose cravat and a small black tricorn hat, and clutches a rolled chart and a quill to "
    "her chest. Anxious and blushing, eyebrows raised, glancing away.")
P["saltmarsh_tavern"] = (
    "The character is a stunning, curvy, flirtatious barmaid in her twenties with wild copper-red curls tumbling over "
    "her shoulders, a dusting of freckles, bright green eyes, full glossy lips and lots of bangles. She wears a low-cut "
    "cream peasant blouse slipping off one shoulder, a tightly laced dark green bodice and a short striped apron, with a "
    "dish rag tossed over her other shoulder. A playful wink and a bubbly laugh, one hand on her hip.")
P["saltmarsh_wright"] = (
    "The character is a sporty, tomboyish, beautiful young woman shipwright in her twenties with a tousled sandy-blond "
    "pixie cut, bright blue eyes, sun-tanned skin, full lips and sawdust on her toned arms. She wears a sleeveless "
    "linen shirt knotted under her chest showing her midriff, an unlaced leather jerkin and a canvas apron at her hips, "
    "and holds a heavy wooden mallet upright in one fist with the head beside her shoulder. A relaxed, confident, "
    "boyish grin.")

# ---- set 5: Greywater and Mournwick ----
P["greywater_harbour"] = (
    "The character is a proud, hot-tempered, beautiful retired navy captain woman in her twenties with long platinum "
    "blonde twin tails, a brass-rimmed eyepatch over one eye, a sharp blue eye and full pouting lips. She wears a fitted "
    "faded navy-blue officer's coat with tarnished gold epaulettes and a row of old campaign medals, open over a white "
    "corset with a low neckline and a white cravat, and holds a brass spyglass tucked under one arm with the other fist "
    "on her hip. She stands rigidly, glaring, cheeks flushed, an indignant tsundere huff.")
P["greywater_tavern"] = (
    "The character is a gaunt, elegant, beautiful widow tavern keeper in her thirties with long jet-black hair split by "
    "a single dramatic white streak, drawn back tightly, cool dark eyes and dark painted full lips. She wears a fitted "
    "black mourning dress with a sheer black lace yoke over a plunging neckline, a corseted waist, a jet cameo brooch and "
    "black lace gloves, and holds a long clay pipe with a thin curl of smoke. A cool, flat, knowing half-smile.")
P["greywater_wright"] = (
    "The character is a stocky, strong, cheerful young woman shipwright in her twenties with a huge mane of bushy auburn "
    "hair braided and bound with iron rings, thick eyebrows, freckles and full lips. She wears a tartan tam-o'-shanter, a "
    "tartan sash across a low-cut heavy wool shirt and a thick leather apron with tools in its pockets, and holds a "
    "caulking iron and mallet up in her hands. Loud and bouncy, a huge open-mouthed laugh.")
P["mournwick_harbour"] = (
    "The character is a pale, beautiful harbourmistress in her twenties dressed as an undertaker, with long sleek black "
    "hair, dark circles under her soft adoring violet eyes, a silver pince-nez, long thin fingers and full dark lips. "
    "She wears a fitted black undertaker's coat over a grey corset waistcoat with a low neckline and a silver watch "
    "chain, a tall black hat with a mourning band, and holds a black-bound register to her chest. A sweet, gravely "
    "polite smile that is a little too fond, her head tilted.")

# ---- set 6: Mournwick and Palmhaven ----
P["mournwick_tavern"] = (
    "The character is an ageless witch of a tavern keeper, an adult woman with an eerily youthful face, long wispy "
    "white hair, one cloudy pale eye, full dark lips, a crooked sly smile and a black raven perched on her shoulder. She "
    "wears layered grey shawls slipping off her shoulders over a dark patched corset dress, many charms and bone "
    "trinkets on strings around her neck, and holds a small glowing lantern close to her chest. Eerie, ancient and "
    "amused, looking at the viewer as if at a child.")
P["mournwick_wright"] = (
    "The character is a tall, slender, gloomy and beautiful woman shipwright and coffin-maker in her twenties with long "
    "lank grey hair, a long pale face, heavy-lidded sorrowful eyes, full lips and a black ribbon tied round her upper "
    "arm. She wears a dusty black waistcoat laced tight over a collarless shirt open at the throat, a carpenter's apron "
    "and a pencil behind her ear, and holds a measuring rule at her chest. Cool, quiet and expressionless.")
P["palmhaven_harbour"] = (
    "The character is a scruffy, tough, beautiful former pirate woman in her twenties with messy sun-bleached blonde "
    "hair, a gold tooth in a cocky grin, tanned skin, full lips and an iron hook in place of her left hand, with a bright "
    "green parrot perched on her shoulder. She wears a battered brown tricorn, a torn sea-green coat over a cropped dirty "
    "shirt that shows her midriff and a pistol tucked in her sash. She leans back like a delinquent, chin up, looking "
    "for a fight.")
P["palmhaven_tavern"] = (
    "The character is a striking, athletic, gorgeous tavern keeper woman in her thirties called Two-Knives, with a short "
    "dark undercut, smoky kohl-rimmed eyes, a scar through one eyebrow, full lips and inked sleeve tattoos of ships and "
    "roses on her toned arms. She wears a fitted sleeveless leather vest with a deep neckline over a loose shirt knotted "
    "at the waist, a red bandana at her throat and two knives in her belt, and crosses her arms under her chest. A "
    "confident, teasing big-sister smirk.")

# ---- set 7: Palmhaven and Coral Bay ----
P["palmhaven_wright"] = (
    "The character is a tall, powerfully built, gorgeous West African woman shipwright in her thirties with dark skin, "
    "short natural hair, high cheekbones, full lips and white geometric tattoos on her bare toned arms and shoulders. "
    "She wears an open canvas vest laced over a wrapped bandeau that shows her cleavage and midriff, a leather apron "
    "with tools at her hips and a necklace of carved wooden beads, and holds a heavy shipwright's axe resting on her "
    "shoulder. Calm, proud and mature, a confident big-sister smile.")
P["coral_harbour"] = (
    "The character is an elegant, beautiful harbourmistress in her thirties with long glossy black hair in a thick "
    "braid over one shoulder, warm brown skin, gold hoop earrings, dark lined eyes and full lips. She wears a wide straw "
    "hat with a red ribbon, a loose white cotton shirt unbuttoned low, a red silk corset sash cinching her slim waist "
    "and a brass compass on a cord between her breasts, and holds a brass spyglass with her other hand on her hip. A "
    "warm, alluring, knowing smile.")
P["coral_tavern"] = (
    "The character is a very tall, curvy, gorgeous Black tavern keeper woman in her twenties with a sleek shaved head, "
    "two gold-capped teeth in a huge grin, gold hoop earrings, full lips and heavy gold rings on her fingers. She wears a "
    "loud red-and-yellow floral blouse tied under her chest over a green brocade corset, and raises a dark rum bottle by "
    "the neck. Loud, bouncy and full of energy, laughing out loud.")
P["coral_wright"] = (
    "The character is a dashing, flirty, beautiful young woman shipwright in her twenties with curly black hair falling "
    "over her forehead, sun-darkened skin, a gold earring, glossy full lips and long painted nails. She wears a cropped "
    "sleeveless striped top that shows her toned midriff, a red headscarf, a coil of rope over one shoulder and lots of "
    "bracelets, and holds a broad machete at her side. A cocky, playful party-girl grin and a peace sign.")

# ---- set 8: Dorada and Ashfall ----
P["dorada_harbour"] = (
    "The character is a vain, haughty, beautiful colonial governor's daughter in her twenties with long golden hair in "
    "elaborate drill curls, a gold monocle, sharp violet eyes and full rouged lips. She wears a crimson velvet gown heavy "
    "with gold embroidery, an off-the-shoulder corseted bodice with a lace jabot over her cleavage and jewelled rings on "
    "every finger, and holds a lace fan to her lips. Looking down her nose, laughing a proud, ringing ojou-sama laugh.")
P["dorada_tavern"] = (
    "The character is a gorgeous tavern owner in her twenties with glossy black hair falling in loose waves, a red "
    "hibiscus flower behind one ear, full red lips and smouldering dark eyes. She wears an off-the-shoulder white blouse, "
    "a tight black corset bodice with a deep neckline, a gold cross pendant at her throat and gold bangles, and holds a "
    "guitar against her side. A sly, sultry, teasing smile.")
P["dorada_wright"] = (
    "The character is a glamorous, voluptuous Creole woman shipwright in her forties with deep laugh lines, a tall "
    "colourful wrapped headscarf, carved bone earrings, many bead necklaces over her cleavage, full lips and a short pipe "
    "in her hand. She wears a fitted faded indigo work dress with the sleeves rolled and a low neckline, a heavy leather "
    "apron at her hips, and holds a carpenter's square. A warm, motherly, amused smile.")
P["ashfall_harbour"] = (
    "The character is a grimy, tough, beautiful harbourmistress in her twenties with frizzy grey-streaked dark hair "
    "under a battered tricorn with a singed feather, a gold tooth, ash smudges on her cheeks and full lips, a pipe in "
    "her teeth. She wears a patched brown greatcoat dusted with grey ash, open over a cropped shirt and a leather corset, "
    "a huge ring of iron keys on her belt, and holds a tin cup. A suspicious, delinquent squint and a scoff.")

# ---- set 9: Ashfall and Cinderport ----
P["ashfall_tavern"] = (
    "The character is a tall, languid, beautiful tavern keeper woman in her twenties with long straight ash-white hair, "
    "pale grey half-lidded eyes, sharp cheekbones, a soot smudge on her pale cheek and full lips. She wears a threadbare "
    "charcoal corset waistcoat over a loose shirt open low, a rust-red silk cravat and fingerless gloves, and polishes a "
    "glass with a rag. Cool and expressionless, a thin, faint smile.")
P["ashfall_wright"] = (
    "The character is a cute, energetic, beautiful young engineer woman in her twenties with short messy dark hair with "
    "a streak of bright blue, soot smudged on her cheeks and nose, full lips and big brass goggles pushed up on her head. "
    "She wears a work shirt tied under her chest showing her midriff, oil-stained suspenders and a tool belt on her hips, "
    "and holds a large wrench over her shoulder. A huge eager grin, bursting with energy.")
P["cinderport_harbour"] = (
    "The character is a sharp, proud, beautiful young magistrate woman in her twenties with straight black hair cut in a "
    "severe bob, sharp golden eyes, a soot smudge on her cheek, full pouting lips and a brass-and-leather smoke mask "
    "hanging at her throat. She wears a fitted dusty black frock coat over a black corset with a low neckline, a heavy "
    "brass chain of office across her chest and a scorched tricorn hat, and holds a sealed scroll with her other fist on "
    "her hip. A cold glare with flushed cheeks, an indignant tsundere huff.")
P["cinderport_tavern"] = (
    "The character is a gorgeous, fierce tavern keeper in her thirties with flame-red hair in a high ponytail, smoky eye "
    "makeup, full red lips and an old burn scar curling down one side of her neck. She wears a brass-buckled black "
    "leather corset over a soot-smudged off-the-shoulder shirt and brass goggles pushed up on her forehead, and holds a "
    "smoking clay mug. A dangerous, teasing big-sister grin.")

# ---- set 10: Cinderport and Frostholm ----
P["cinderport_wright"] = (
    "The character is a tall, muscular, tomboyish beautiful woman smith and shipwright in her twenties with a short "
    "black buzz cut, soot on her face, full lips and a riveted iron prosthetic forearm with brass joints in place of her "
    "right arm. She wears dark smoked goggles on her forehead and a heavy leather apron over a black sports bandeau and "
    "bare scarred shoulders, and holds a forge hammer at her chest. A relaxed, blunt, boyish half-grin.")
P["frostholm_harbour"] = (
    "The character is a tall, striking Nordic harbourmistress in her twenties with a long ash-blonde braid threaded with "
    "silver rings, pale blue eyes, windburned cheeks, full lips and thin blue tattooed lines down her chin. She wears a "
    "fitted grey wool coat with a white wolf-fur collar, open over a laced leather bodice with a low neckline, a silver "
    "knotwork brooch and fur-lined gloves, and holds a wooden tally board with her other fist on her hip. A proud, "
    "measuring glare and a pout, cheeks pink.")
P["frostholm_tavern"] = (
    "The character is a big, strong, gorgeous Nordic tavern keeper woman in her twenties with a long red-gold mane in "
    "two thick braids, blue runic tattoos down her arms and collarbone, freckles and full lips, a bear-fur mantle over "
    "her shoulders. She wears a laced leather corset over a low-cut wool tunic and leather bracers, and raises a carved "
    "drinking horn. Roaring with laughter, loud and full of energy.")
P["frostholm_wright"] = (
    "The character is a beautiful, muscular young Nordic shipwright woman in her twenties with shaved sides, a long "
    "platinum-blonde braided topknot, ice-blue eyes, a few frost-reddened freckles and full lips. She wears a sleeveless "
    "fur-trimmed leather tunic with a deep neckline that shows her strong arms, wrist wraps and a thick tool belt, and "
    "holds a bearded axe upright in her raised fist, the axe head beside her shoulder. A fearless smirk, cheeks a "
    "little flushed, refusing to look impressed.")

# ---- set 11: Iceward Point ----
P["iceward_harbour"] = (
    "The character is a graceful, gentle, beautiful Nordic priestess in her twenties with very long straight white hair, "
    "pale skin, one eye clouded milky white and the other soft blue, and full lips. She wears a fitted long black "
    "clerical coat with a white collar open over a black corseted dress, a heavy white wool scarf and a carved bone "
    "pendant, and holds a small oil lantern close to her chest in both hands. Calm, refined and kind, a soft polite smile.")
P["iceward_tavern"] = (
    "The character is a voluptuous, rosy-cheeked, gorgeous Nordic tavern keeper in her thirties with long grey-blonde "
    "plaits, warm eyes, full lips and a tall embroidered fur hat with ear flaps. She wears a fitted embroidered red wool "
    "dress with a laced bodice and a low neckline, a sheepskin vest and an apron, and holds a steaming wooden bowl of "
    "stew and a ladle. A warm, motherly, indulgent smile.")
P["iceward_wright"] = (
    "The character is a grim, proud, beautiful Nordic woman shipwright in her twenties with a carved whalebone eyepatch, "
    "long white-streaked dark hair, a seal-fur hood pushed back, full pouting lips and scrimshaw-like tattoos on her "
    "hands. She wears a fitted heavy oilskin coat open over a fur-trimmed leather bodice and a belt of whalebone tools, "
    "and holds a bone-handled drawknife at her chest. A glowering pout, cheeks pink, refusing to look at the viewer.")
P["dread_corwin"] = (
    "The character is a strikingly beautiful, cruel gentlewoman pirate captain in her thirties with long powdered white "
    "hair tied with a black ribbon, a beauty mark, icy grey eyes, a thin scar across her full lips. She wears a pale "
    "lavender silk coat with silver embroidery over a fitted lavender corset with a plunging lace neckline, a lace "
    "cravat and a jewelled rapier at her hip, and holds a silver snuff box delicately. A slow, cold, amused smirk that "
    "enjoys your discomfort.")

# ---- set 12: the dread pirates and the navy ----
P["dread_kate"] = (
    "The character is a wildly beautiful pirate captain woman in her twenties with a mane of singed black hair with "
    "ember-orange tips, a burn scar across one cheek, fiery amber eyes, soot-smudged skin and full red lips. She wears a "
    "scorched black leather coat open over a red corset with a low neckline, a bandolier of round iron grenades across "
    "her chest, and holds a lit torch close to her face. A sweet, adoring, unhinged smile with wide eyes.")
P["dread_rime"] = (
    "The character is a cold, ancient-eyed, beautiful ice pirate captain woman in her twenties with very long white hair "
    "hung with icicles, pale blue frost creeping across her cheeks, one eye clouded white, full pale blue lips and "
    "frost-blackened fingertips. She wears a heavy white bear-fur cloak over a fitted battered iron breastplate shaped to "
    "her figure and a fur hat, and holds a barbed whaling harpoon upright against her shoulder. A flat, cold, "
    "expressionless stare.")
P["dread_widow"] = (
    "The character is an eerily beautiful pale pirate captain woman in her twenties with long silver-grey hair, "
    "colourless pale eyes, ashen full lips and a black lace veil over the lower half of her face. She wears a tattered "
    "grey captain's coat over a faded mourning gown with a corseted bodice and a low lace neckline, a tarnished silver "
    "locket, and holds a thin rapier upright before her. A sweet, adoring, chillingly calm gaze, head tilted.")
P["sea_navy"] = (
    "The character is a striking, beautiful navy commodore woman in her thirties with dark hair tied in a neat queue, "
    "high cheekbones, full lips and a cool, intelligent gaze. She wears a tailored navy-blue officer's coat with gold "
    "lace and epaulettes over a fitted white corset waistcoat with a low neckline and a black bicorne hat, and rests a "
    "hand on her sword hilt at her hip. Crisp, commanding and expressionless.")

# ---- set 13: the people met at sea ----
P["sea_merchant"] = (
    "The character is a rich, haughty, beautiful merchant's daughter and merchant captain in her twenties with long "
    "honey-blonde hair in ringlets, rosy cheeks, a gold monocle, full rouged lips and a gold watch chain. She wears a fine "
    "brown frock coat over an embroidered gold corset waistcoat with a low neckline and a black tricorn with a feather, "
    "and holds a rolled bill of lading like a fan. A proud, ringing ojou-sama laugh, nose in the air.")
P["sea_pirate"] = (
    "The character is a tough, weathered, beautiful pirate captain woman in her twenties with a long braided red "
    "ponytail, a scar across her cheek, sharp green eyes and full lips. She wears a navy blue long coat with tarnished "
    "gold buttons over a white linen shirt knotted under her chest and a black leather corset, and a black tricorn hat, "
    "and rests one hand on the hilt of a cutlass at her hip. A cocky delinquent smirk, chin up, looking for a fight.")
P["sea_castaway"] = (
    "The character is a ragged, sunburnt, beautiful castaway sailor woman in her twenties with wild long sun-bleached "
    "hair full of salt, freckles, full lips and a bead necklace of shells. She wears the torn remains of a striped shirt "
    "knotted under her chest showing her midriff and a frayed sash, and holds a cracked coconut up in both hands. A huge, "
    "overjoyed open-mouthed smile, bursting with energy.")
P["sea_captive"] = (
    "The character is a beautiful, defiant young noblewoman in her twenties with tousled chestnut hair coming loose from "
    "its pins, a smudge on her cheek, bright hazel eyes and full pouting lips. She wears a torn pale blue silk gown with "
    "a lace corset bodice and a low neckline, an oversized officer's coat over her bare shoulders, and holds a small "
    "pistol close to her chest. Proud and unbroken, an indignant glare with pink cheeks.")

# ---- set 14: at sea and the navy ----
P["sea_captive_m"] = (
    "The character is a quiet, beautiful young ship's officer woman in her twenties with tousled short dark hair, a "
    "bruised cheek, a split full lip and calm dark eyes. She wears a torn white shirt open low over a fitted waistcoat, "
    "a ripped blue midshipman's coat with brass buttons and frayed rope still knotted round one wrist, and rubs her "
    "wrist. Cool and composed, a faint, grateful half-smile.")
P["navy_1"] = (
    "The character is a tall, stiff, beautiful naval captain woman in her twenties with long powdered silver hair tied "
    "with a black ribbon, sharp grey eyes and full pouting lips. She wears a fitted dark blue naval coat with white "
    "lapels, gold epaulettes and buttons over a white corset waistcoat with a low neckline, and a sword at her hip, one "
    "fist on her hip. A cold, disciplined glare that can't hide her flushed cheeks.")
P["navy_3"] = (
    "The character is a glamorous, voluptuous lady admiral in her forties with long wavy white hair, a scar across one "
    "cheek, a twinkle in her blue eyes, full red lips and a heavy gold-laced bicorne hat. She wears a heavily decorated "
    "dark blue coat with medals and gold braid open over a white corseted bodice with a plunging neckline, and leans on "
    "a cane. Battle-hardened and amused, a teasing big-sister smile.")
P["navy_4"] = (
    "The character is a tall, beautiful naval commander woman in her twenties with dark auburn hair in a tight queue "
    "under a black bicorne hat, grey eyes and full lips. She wears a fitted dark blue naval coat with white lapels and "
    "gold buttons over a white corset with a low neckline, a gold epaulette on one shoulder, a sword belt and a brass "
    "telescope tucked under one arm. Stern, fair and expressionless, utterly in command.")

# ---- set 15: the navy and the fleet ----
P["navy_5"] = (
    "The character is a dashing, athletic, beautiful marine officer woman in her twenties with a short dark ponytail, a "
    "duelling scar on her chin, bright eyes and full lips. She wears a fitted scarlet marine coat with white crossbelts "
    "over her chest and a black stock, open over a white corset with a low neckline, and holds a musket upright in one "
    "fist by her shoulder. Brave and a little reckless, a big excited grin.")
P["navy_6"] = (
    "The character is a severe, beautiful navy post-captain woman in her twenties with long steel-grey hair in a tight "
    "queue, a lean face, piercing pale eyes and full pouting lips. She wears an immaculate fitted dark blue coat with "
    "gold lace over a white corset waistcoat with a low neckline, a bicorne under her arm, and holds a telescope raised "
    "in one fist by her shoulder. Cold and exacting, an irritated glare with faintly pink cheeks.")
P["fleet_2"] = (
    "The character is a glamorous, voluptuous old sea dog captain woman in her forties with long wavy grey hair, a clay "
    "pipe at her full red lips and her left coat sleeve empty and pinned up at the shoulder. She wears a weathered dark "
    "green captain's coat over a laced leather corset with a plunging neckline and a battered tricorn, and rests her one "
    "hand on a cutlass at her hip. Wry and unbothered, a slow, indulgent, motherly smile.")
P["fleet_3"] = (
    "The character is a gorgeous Black ship's captain woman in her thirties with long box braids, a gold earring, full "
    "lips and a confident gaze. She wears an open maroon captain's coat with gold trim over a white linen shirt "
    "unbuttoned low and a leather corset, a leather baldric, and holds a cutlass resting against her shoulder. Easy and "
    "assured, a teasing big-sister smile.")

# ---- set 16: the fleet ----
P["fleet_4"] = (
    "The character is a sharp-eyed, beautiful navigator woman in her twenties with a long black braid, high cheekbones, "
    "a thin scar on her chin and full lips. She wears a fitted brown leather sea coat open over a dark blue laced bodice "
    "with a low neckline, a brass compass on a chain and fingerless gloves, and holds a rolled chart at her chest. Calm, "
    "focused and expressionless.")
P["fleet_5"] = (
    "The character is a tough, beautiful former pirate turned privateer woman in her twenties with a short black "
    "undercut, sea-serpent tattoos on her neck and shoulders, a gold nose ring and full dark lips. She wears a long black "
    "leather coat with silver studs over a dark red corset, a wide belt with a cutlass and two flintlock pistols, and a "
    "gold chain with a shark-tooth pendant, arms folded under her chest. A sly grin, cheeks pink, pretending not to care.")
P["fleet_6"] = (
    "The character is a dignified, graceful and beautiful lady captain in her thirties with long straight white hair, "
    "round spectacles, kind eyes and full lips. She wears an immaculate royal-blue captain's coat with silver buttons "
    "and lace cuffs over a fitted corseted dress, a cravat and a black tricorn with silver trim, an open pocket watch in "
    "one hand and a polished brass telescope under her arm. Elegant, calm and polite, a wise, gentle smile.")
P["fleet_7"] = "(the user's own captain, drawn from the user's likeness: the prompt is kept off the repo)"

# ---- set 17: merchant captains ----
P["merchant_1"] = (
    "The character is a cheerful, curvy, beautiful merchant captain woman in her twenties with rosy cheeks, long wavy "
    "chestnut hair under a battered tricorn hat, freckles and full lips. She wears a bottle-green wool coat with brass "
    "buttons open over a mustard corset waistcoat with a low neckline, and holds a rolled bill of lading and a heavy "
    "purse of coins up in her hands. A huge, bubbly, excited grin, sure she can talk her way out of anything.")
P["merchant_2"] = (
    "The character is a capable, beautiful merchant captain woman in her thirties with sun-kissed skin, auburn hair in a "
    "thick braid over one shoulder, a few freckles, sharp grey eyes and full lips. She wears a navy blue double-breasted "
    "coat open over a loose white shirt unbuttoned low and a laced corset, a leather belt with a brass spyglass, one hand "
    "on her hip. A wry, teasing big-sister smile.")
P["merchant_4"] = (
    "The character is a striking, gorgeous West African merchant captain woman in her thirties with dark skin, high "
    "cheekbones, short cropped hair, gold stud earrings and full lips. She wears a long teal coat open over a cream "
    "corset blouse with a low neckline and a patterned silk sash at her waist, with a ledger tucked under one arm and her "
    "other hand on her hip. A confident, appraising, knowing smile.")
P["merchant_5"] = (
    "The character is a wiry, tough, beautiful merchant captain woman in her twenties with white hair in a long plait, "
    "a tanned face, full lips and a clay pipe between her teeth. She wears a faded green coat with brass buttons over a "
    "cropped knitted jumper, canvas trousers and sea boots, a battered leather ledger under her arm. A salty, delinquent "
    "smirk, chin up.")

# ---- set 18: merchants and rivals ----
P["merchant_6"] = (
    "The character is a glamorous, haughty Venetian merchant captain woman in her twenties with honey-blonde hair in "
    "drill curls under a small plumed hat, a beauty mark, full rouged lips and pearls at her throat. She wears a rich "
    "emerald brocade coat over a low-cut cream corset blouse and holds a fan of banknotes raised in one hand by her "
    "shoulder. A proud, ringing ojou-sama laugh behind her banknotes.")
P["merchant_7"] = (
    "The character is a tall, voluptuous, gorgeous Ottoman merchant captain woman in her thirties with long black hair "
    "under a white silk head wrap, kind dark eyes lined with kohl and full lips. She wears a long saffron kaftan open at "
    "a plunging neckline with a gold-embroidered corset sash at her waist and gold jewellery, and holds a string of amber "
    "prayer beads in one raised hand. A warm, gentle, motherly smile.")
P["rival_1"] = (
    "The character is a glamorous, dangerous rival captain woman in her thirties with long wavy black hair, a beauty "
    "mark, full dark red lips and half-lidded eyes. She wears a fitted crimson frock coat with gold embroidery over a "
    "black corset with a plunging neckline, a black tricorn with a white plume and a rapier at her hip, one gloved hand "
    "on the hilt. A slow, amused, sadistic smirk.")
P["rival_2"] = (
    "The character is a cocky, beautiful rival privateer woman in her twenties with messy sandy-blonde hair in a side "
    "ponytail, a scar through one eyebrow, bright eyes and full pouting lips. She wears a dark green long coat over an "
    "open white shirt knotted under her chest, a baldric with two pistols, one fist on her hip. A cocky grin with "
    "flushed cheeks, pretending she isn't impressed.")

# ---- set 19: rival captains ----
P["rival_3"] = (
    "The character is an elegant, glamorous rival captain, a mature woman in her forties with silver hair in a severe "
    "bun, a monocle, cool grey eyes, full dark lips and a thin cigarillo. She wears an immaculate fitted black coat with "
    "silver buttons and a high collar, open at a black corset with a low neckline, and black leather gloves. Cool, "
    "calculating and expressionless, the faintest amusement.")
P["rival_4"] = (
    "The character is a big, strong, gorgeous rival captain woman in her twenties with a huge mane of red hair braided "
    "and decorated with beads, freckles, full lips, a fur-trimmed coat open over a leather corset with a low neckline, "
    "and a big brass-handled axe on her shoulder. She laughs with her head thrown back, loud, bouncy and friendly in a "
    "threatening way.")
P["rival_5"] = (
    "The character is a lithe, beautiful, bratty young rival captain woman in her twenties with a shaved undercut, a long "
    "black braid, kohl-rimmed eyes, gold rings in her ears and glossy full lips. She wears a sleeveless leather jerkin "
    "laced over a loose cropped shirt that shows her midriff, a cutlass in hand. A smug, mocking grin, sticking her "
    "tongue out at the viewer.")
P["rival_6"] = (
    "The character is a cold, beautiful rival captain woman in her twenties with sleek slicked-back dark hair, pale icy "
    "eyes and full lips. She wears a fitted midnight blue coat with a high collar over a black corset with a low "
    "neckline, a silver skull brooch, and holds a cocked pistol casually. Menacing calm, a flat expressionless stare.")

# ---- set 20: smugglers ----
P["smuggler_1"] = (
    "The character is a shy, pretty smuggler woman in her twenties in a hooded grey cloak, with a few strands of dark "
    "hair, quick nervous dark eyes and full lips. Under the cloak a fitted patched corset waistcoat with too many pockets, "
    "and a cask of brandy hugged under one arm. Timid and blushing, peeking out from under her hood with a small smile.")
P["smuggler_2"] = (
    "The character is a cute, cheeky, beautiful smuggler woman in her twenties with honey-brown hair under a dark knit "
    "cap, freckles and glossy full lips. She wears a short dark coat open over a cropped shirt with rolled sleeves, a "
    "leather satchel stuffed with letters and a knife in her boot. A bratty, teasing grin and a wink, mocking the viewer.")
P["smuggler_3"] = (
    "The character is a glamorous, voluptuous mature smuggler woman in her forties with long grey hair in two braids, a "
    "black eyepatch, full red lips and a pipe. She wears an oilskin coat open over a laced corset with a plunging "
    "neckline and a lantern hanging from her belt. Knowing and unafraid, a slow, honeyed, indulgent smile.")
P["smuggler_4"] = (
    "The character is a sharp-featured, beautiful smuggler woman in her twenties with black hair cut to the jaw, a thin "
    "white scar on her chin, dark watchful eyes and full pouting lips. She wears a long weathered brown greatcoat with "
    "deep pockets over a fitted grey corset waistcoat, fingerless gloves, a coil of rope over one shoulder and a small "
    "keg under her arm. A prickly glare with pink cheeks, quietly dangerous.")

# ---- set 21: smugglers and castaways ----
P["smuggler_5"] = (
    "The character is a cheerful, curvy, beautiful smuggler woman in her twenties with rosy cheeks, curly blonde hair "
    "under a red headscarf, a gold tooth in her grin and full lips. She wears a patched blue apron over a laced bodice "
    "with a low neckline and a striped skirt, a heavy shawl, and a basket of 'fish' on her hip with bottles poking out. "
    "A sweet, bubbly, innocent smile with wicked eyes.")
P["smuggler_6"] = (
    "The character is a roguish, tomboyish, beautiful Cornish smuggler woman in her twenties with short curly black "
    "hair, a gap-toothed grin with a gold tooth and full lips. She wears a fitted knitted fisherman's sweater, a short "
    "pea coat and a knitted cap, and holds a small brandy cask on her shoulder in one fist. Cheeky and laid-back, a "
    "casual boyish grin.")
P["smuggler_7"] = (
    "The character is a beautiful, graceful Moroccan smuggler woman in her twenties with kohl-lined amber eyes, a nose "
    "chain, full lips and long dark hair under a loose indigo scarf. She wears a fitted indigo vest laced over a white "
    "blouse with a low neckline, a belt of small pouches and a curved dagger, and holds a silk-wrapped bundle raised in "
    "one hand by her shoulder. Refined, secretive and polite, a soft gentle smile.")
P["castaway_1"] = (
    "The character is a wild, beautiful castaway woman in her twenties alone on an island for years, with sun-bleached "
    "tangled hair, freckled tanned skin, bright adoring eyes and full lips. She wears a ragged faded sky-blue naval coat "
    "several sizes too big slipping off her shoulders over a torn linen shirt knotted under her chest, a shell necklace, "
    "and clutches a hand-made spear and a corked message bottle. A sweet, adoring smile that is a little unhinged, "
    "tears of joy in her eyes.")

# ---- set 22: castaways ----
P["castaway_2"] = (
    "The character is an ageless castaway, an adult woman with an eerily youthful face, very long white hair, sunburnt "
    "skin, full lips and wise old eyes. She wears a patched, faded scarlet naval officer's coat over a ragged shirt and a "
    "hat woven from palm leaves, and clutches a battered leather journal and a tiny carved wooden ship. Dazed and kindly, "
    "an old, amused little smile as if she has seen everything.")
P["castaway_3"] = (
    "The character is a lean, wiry, beautiful young castaway sailor woman in her twenties with shaggy black hair, a red "
    "bandana, a parrot feather tucked behind one ear, tanned skin and full lips. She wears a torn striped shirt knotted "
    "under her chest showing her midriff and ragged short trousers, a coconut shell cup raised in her hand. Grinning "
    "wildly, bursting with energy.")
P["castaway_4"] = (
    "The character is a sturdy, tomboyish, beautiful castaway sailor woman in her twenties with brown hair hacked short, "
    "a sunburnt face, full lips and strong toned arms. She wears a torn and faded red naval jacket open over a sailcloth "
    "chest wrap, wrapped with a sailcloth belt, a long knife made from a barrel hoop and a spyglass with a cracked lens. "
    "Capable and blunt, a relieved boyish grin.")
P["castaway_5"] = (
    "The character is a sweet, beautiful young castaway woman in her twenties raised on an island, with long wild dark "
    "curly hair full of salt, freckled brown skin, wide amazed eyes and full lips. She wears a ragged yellowed linen "
    "dress cut short at the knee with a low neckline, a necklace of shells and shark teeth, and holds a woven palm-leaf "
    "hat. A warm, open, affectionate smile, cheeks pink.")

# ---- set 23: castaways and captives ----
P["castaway_6"] = (
    "The character is a proud, beautiful castaway navy midshipman woman in her twenties, sunburnt and slender, with "
    "matted fair hair in a messy short ponytail, full pouting lips and hopeful eyes. She wears the shredded remains of a "
    "blue naval jacket with one gold button left over a torn white shirt knotted under her chest, and clutches a bent "
    "brass spyglass to her chest. Shaken but indignant, cheeks flushed, glaring as if she didn't need rescuing.")
P["castaway_7"] = (
    "The character is a broad-shouldered, tomboyish, beautiful castaway whaler woman in her twenties with cropped "
    "iron-grey hair, frost-reddened cheeks, full lips and a fur-lined hood pushed back. She wears a patched oilskin coat "
    "open over a fitted wool jumper, and holds a broken harpoon shaft raised in one fist by her shoulder. Stubborn and "
    "blunt, a relaxed boyish grin.")
P["castaway_8"] = (
    "The character is a voluptuous, gorgeous castaway ship's cook woman in her thirties with a round pretty face, black "
    "hair tied up in a messy bun, full lips and a torn apron stained with salt over a ragged white blouse with a low "
    "neckline. She holds a dented iron ladle raised in one fist by her shoulder like a weapon. Exhausted but laughing, a "
    "warm, motherly, indulgent smile.")
P["castaway_9"] = (
    "The character is a beautiful castaway woman in her twenties with long sun-streaked copper hair full of salt, "
    "freckled tanned skin, grey eyes and full lips. She wears a torn and faded green silk gown cut short at the knee "
    "with a corseted bodice and bare shoulders, a gold locket, and holds a driftwood spear raised in one fist by her "
    "shoulder. Wary and relieved, a teasing, confident big-sister smile.")

# ---- set 24: freed captives ----
P["captive_f_1"] = (
    "The character is a beautiful young noblewoman in her twenties freed from pirates, with dark ringlets coming loose, "
    "flushed cheeks, defiant green eyes and full pouting lips. She wears a torn pale blue silk gown with a lace corset "
    "bodice and a low neckline, a pirate's coat thrown over her bare shoulders and a broken rope around one wrist. Proud "
    "and furious, an indignant tsundere huff, arms crossed.")
P["captive_f_2"] = (
    "The character is a shy, scholarly, beautiful woman in her twenties with long grey-streaked brown hair in a loose "
    "bun, round spectacles, ink-stained fingers and full lips. She wears a fitted dusty dark green travelling dress with "
    "a corseted waist and a modest low neckline, and clutches a rolled chart to her chest. Timid and blushing, peeking "
    "over the chart with curious eyes.")
P["captive_f_3"] = (
    "The character is a tough, beautiful sailor woman in her twenties with short copper hair, a bruise on one cheek and "
    "a split full lip, smiling anyway. She wears a ripped white shirt open low over a leather corset vest and sailor's "
    "trousers, rubbing her freed wrists. A cocky delinquent grin, chin up, looking for a fight.")
P["captive_f_5"] = (
    "The character is a calm, beautiful young East Asian woman in her twenties, a ship's surgeon, with straight black "
    "hair tied back, a calm intelligent face, a smudge of soot on her cheek and full lips. She wears a fitted dark grey "
    "dress with rolled sleeves and a low square neckline, a leather apron and a satchel of medical instruments over her "
    "shoulder. Composed and cool, a faint, kind expression.")

# ---- set 25: freed captives ----
P["captive_f_6"] = (
    "The character is a fierce, beautiful young noblewoman captive in her twenties with dark ringlets coming loose, "
    "flushed cheeks, fierce brown eyes, a bruise on one cheekbone and full pouting lips. She wears a torn crimson silk "
    "dress with a laced corset bodice and a low neckline, and a rope still tied round one wrist, which she raises in a "
    "fist by her shoulder. Furious and unafraid, an indignant tsundere glare.")
P["captive_f_7"] = (
    "The character is a proud, beautiful Chinese navigator woman in her twenties with a sleek black bob, sharp features, "
    "ink-stained fingers and full lips. She wears a fitted dark blue quilted jacket open over a silk qipao-style bodice "
    "with a keyhole neckline, a jade pendant, and holds a rolled sea chart raised in one fist by her shoulder. Calculating "
    "and proud, a haughty pout with pink cheeks.")
P["captive_m_1"] = (
    "The character is a pompous, beautiful young noblewoman in her twenties with pale skin, long fair hair in "
    "elaborate drill curls coming undone, sharp blue eyes and full rouged lips. She wears a stained embroidered silk "
    "gown with a corseted bodice and a low neckline, a torn lace collar and only one shoe in her hand. Hungry and "
    "indignant, a proud ojou-sama laugh behind her hand.")
P["captive_m_2"] = (
    "The character is a broad-shouldered, strong, beautiful ship's gunner woman in her twenties with a short black buzz "
    "cut, tattooed toned arms and full lips. She wears a torn grey shirt knotted under her chest showing her abs and "
    "canvas trousers, iron shackles hanging open from her wrists. Grim and cool, a flat stare, ready for revenge.")

# ---- set 26: freed captives and pirates ----
P["captive_m_3"] = (
    "The character is a gentle, shy, beautiful young ship's surgeon woman in her twenties with tousled black hair, round "
    "spectacles with one cracked lens, a soft face and full lips. She wears a blood-flecked white blouse with rolled "
    "sleeves and a fitted leather corset waistcoat, and holds a small leather medical bag raised in one fist by her "
    "shoulder. Earnest and nervous, blushing, a timid little smile.")
P["captive_m_4"] = (
    "The character is a voluptuous, gorgeous Basque harbour pilot woman in her thirties with thick dark wavy hair, "
    "strong eyebrows, full red lips and a red beret. She wears a fitted heavy wool sweater with a deep neckline and a "
    "sailor's coat with the buttons torn off, and holds a knotted sounding line raised in one fist by her shoulder. A "
    "warm, motherly, grateful smile.")
P["pirate_1"] = (
    "The character is a brutal, wild, beautiful pirate captain woman in her twenties with gold teeth in a snarl, a red "
    "bandana, a long messy black braid tied with red ribbons and little bones, and full lips. She wears a filthy crimson "
    "frock coat over a stained white shirt knotted under her chest and a black leather corset, a bandolier of flintlock "
    "pistols across her chest and a cutlass in her fist. A delinquent sneer, mid-shout, looking for a fight.")
P["pirate_2"] = (
    "The character is a beautiful, bratty, sneering pirate captain woman in her twenties with wild windswept blonde hair "
    "under a tricorn heaped with ostrich feathers, smoky eyes, a beauty mark and glossy full lips. She wears a low-cut "
    "black corset under a long red coat, with three pistols tucked in a leather bandolier, and holds one pistol up "
    "beside her face. A smug, mocking grin, laughing at how weak the viewer is.")

# ---- set 27: pirates ----
P["pirate_3"] = (
    "The character is a huge, muscular, beautiful pirate woman in her twenties with a shaved head, a riveted iron plate "
    "along one side of her jaw, cold small eyes and full lips. She wears a heavy rust-orange leather longcoat with iron "
    "studs over a fitted chain-mail bodice, thick chains wrapped round her forearms, and a huge boarding axe in her fist. "
    "Silent and looming, an expressionless stare.")
P["pirate_4"] = (
    "The character is a sinister, beautiful pirate captain woman in her twenties with her face painted white like a "
    "skull, black-ringed adoring eyes, long black hair and black lips. She wears a tall battered top hat with a raven "
    "feather, a black frock coat over a black corset with a low neckline and a necklace of teeth, and holds a long slim "
    "dagger at her chest. A sweet, unsettling, lovestruck smile.")
P["pirate_5"] = (
    "The character is a flamboyant, beautiful young pirate captain woman in her twenties with tousled dark hair, gold "
    "earrings, kohl-lined eyes and full glossy lips. She wears a feathered tricorn, a silk shirt open low under a teal "
    "coat and a corset, and rests a cutlass on her shoulder. A slow, teasing, sadistic wink.")
P["pirate_6"] = (
    "The character is a scarred, beautiful pirate captain woman in her twenties with half her face hidden behind a "
    "cracked white porcelain mask, one cold blue eye showing, long grey hair and full lips. She wears a heavy black "
    "greatcoat with tarnished silver buttons over a black corset, a dark purple sash and black gloves, and holds a "
    "flintlock pistol. Cold, theatrical and unreadable.")

# ---- set 28: pirates ----
P["pirate_7"] = (
    "The character is a wiry, beautiful Malay pirate woman in her twenties with a shaved head, intricate black tattoos "
    "down her neck and arms, a thin scar through one eyebrow and full dark lips. She wears a cropped dark red vest with a "
    "low neckline, a wide black sash stuffed with throwing knives and loose black trousers, and holds a curved kris "
    "raised in one fist by her shoulder. A cold, amused, sadistic smirk.")
P["pirate_8"] = (
    "The character is a big, strong, gorgeous Norse pirate woman in her twenties with a long braided red-gold mane, blue "
    "woad painted across her face and shoulders, freckles and full lips. She wears a sleeveless bearskin over a fitted "
    "rusted mail bodice, iron arm rings, and holds a short boarding axe raised in one fist beside her shoulder. Roaring "
    "with laughter, loud and full of energy.")
P["pirate_9"] = (
    "The character is a charming, beautiful young French pirate woman in her twenties with long wavy chestnut hair, a "
    "beauty mark, a single pearl earring and full glossy lips. She wears an unbuttoned lace-cuffed blouse over a fitted "
    "lilac brocade corset and a faded lilac coat, and holds a rapier raised upright in one fist by her shoulder. A "
    "charming, dangerous, amused smile that enjoys the viewer's discomfort.")
P["pirate_10"] = (
    "The character is a tall, gorgeous Black Caribbean pirate quartermistress in her thirties with long locs tied up in "
    "a crimson headwrap, gold rings in both ears, a gold tooth and full lips. She wears a sleeveless black leather "
    "bodice with a plunging neckline, a bandolier of pistols across her chest and a short cutlass raised in one fist by "
    "her shoulder. Fierce and proud, a confident big-sister grin.")

# ---- set 29: pirates ----
P["pirate_11"] = (
    "The character is a half-mad, beautiful pirate gunner woman in her twenties with wild grey-white hair singed at the "
    "ends, soot on her face, one eye clouded white and the other gleaming, full lips and a peg-leg. She wears a "
    "powder-burnt leather apron over a striped cropped shirt and holds a smoking linstock raised high like a magic "
    "staff. A dramatic, grandiose cackle, one hand over her clouded eye.")
P["pirate_12"] = (
    "The character is a pale, beautiful pirate woman in her twenties with jet black hair in a long braid, dark "
    "lipstick, kohl-rimmed green eyes and a silver nose ring. She wears a black tricorn with a raven feather, a tight "
    "black corset under a long tattered coat, and holds a flintlock pistol raised in one fist by her shoulder. A sweet, "
    "adoring, wicked smile, head tilted.")
P["merchant_3"] = (
    "The character is a lean, cool, beautiful Portuguese merchant captain woman in her twenties with dark curly hair, "
    "gold hoop earrings, clever dark eyes and full lips. She wears a fitted burgundy coat with frayed gold trim over a "
    "shirt open low and a laced corset, and holds a small brass sextant. Cautious and clever, a calm expressionless gaze.")

# ---- redo r1 (the user, set 9: "Redo b c redo") ----
# ashfall_tavern read as a man in all three (lean, flat, angular): softer, curvier, unmistakably a woman
P["ashfall_tavern"] = (
    "The character is a tall, languid, gorgeous tavern keeper woman in her twenties with a soft feminine face, long "
    "straight ash-white hair falling over one shoulder, long eyelashes, pale grey half-lidded eyes, full glossy lips "
    "and a curvy hourglass figure with a large bust. She wears a fitted charcoal corset waistcoat laced tight at her "
    "waist over a white blouse open in a deep neckline, a rust-red silk ribbon at her throat and fingerless lace "
    "gloves, and polishes a glass with a rag. Cool and expressionless, a faint, lazy smile.")
# cinderport_tavern: the soot blotches read as bruises on her skin - a clean face and shoulders, the scar small
P["cinderport_tavern"] = (
    "The character is a gorgeous, fierce tavern keeper in her thirties with flame-red hair in a high ponytail, smoky "
    "eye makeup, full red lips, clean smooth skin and a thin faded burn scar on one side of her neck. She wears a "
    "brass-buckled black leather corset with a plunging neckline over a white off-the-shoulder blouse and brass "
    "goggles pushed up on her forehead, and holds a smoking clay mug. A dangerous, teasing big-sister grin, one hand "
    "on her hip.")
# cinderport_tavern again (the user: "Cinderport sot was good she just didnt have visible part of legs"): the soot
# back as it was, and framed down to mid-thigh like the rest (hips and the top of her legs in the picture)
P["cinderport_tavern"] = (
    "Framed from the head down to mid-thigh, her hips and the top of her legs in the picture. The character is a "
    "gorgeous, fierce tavern keeper in her thirties with flame-red hair in a high ponytail, smoky eye makeup, red lips "
    "and an old burn scar curling down one side of her neck. She wears a brass-buckled black leather corset over a "
    "soot-smudged off-the-shoulder shirt, tight dark leather trousers with a wide belt, and brass goggles pushed up on "
    "her forehead, and holds a smoking clay mug. A dangerous, teasing big-sister grin, one hand on her hip.")
# cinderport_tavern back to her first prompt (the user: "Use same prompt and hope for luck with new seed")
P["cinderport_tavern"] = (
    "The character is a gorgeous, fierce tavern keeper in her thirties with flame-red hair in a high ponytail, smoky eye "
    "makeup, full red lips and an old burn scar curling down one side of her neck. She wears a brass-buckled black "
    "leather corset over a soot-smudged off-the-shoulder shirt and brass goggles pushed up on her forehead, and holds a "
    "smoking clay mug. A dangerous, teasing big-sister grin.")

# ---- redo r1, set 10 (the user: "Redo c a redo") ----
# cinderport_wright read butch (buzz cut, bodybuilder): still the tomboy smith with the iron arm, but plainly a woman
P["cinderport_wright"] = (
    "The character is a tomboyish, beautiful young woman smith and shipwright in her twenties with a messy black pixie "
    "cut with long side bangs, a soft pretty face with soot on one cheek, long eyelashes, full lips, a toned curvy "
    "figure with a large bust, and a riveted iron prosthetic forearm with brass joints in place of her right arm. She "
    "wears dark smoked goggles on her forehead and a heavy leather apron over a black bandeau that shows her cleavage "
    "and bare shoulders, and holds a forge hammer at her chest. A relaxed, blunt, boyish half-grin.")
# frostholm_wright: the same prompt, new seeds
# frostholm_wright: the hairstyle (the user: "Hairstyle on frostholm") - no shaved sides and topknot; long loose
# platinum hair with a thin braid at one temple
P["frostholm_wright"] = (
    "The character is a beautiful, muscular young Nordic shipwright woman in her twenties with long, thick, loose "
    "platinum-blonde hair falling past her shoulders and a thin braid at one temple, ice-blue eyes, a few frost-reddened "
    "freckles and full lips. She wears a sleeveless fur-trimmed leather tunic with a deep neckline that shows her strong "
    "arms, wrist wraps and a thick tool belt, and holds a bearded axe upright in her raised fist, the axe head beside "
    "her shoulder. A fearless smirk, cheeks a little flushed, refusing to look impressed.")
