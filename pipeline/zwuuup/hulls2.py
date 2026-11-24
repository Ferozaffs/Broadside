"""Second go at bare hulls (2026-11-21): the first wording ("NO masts...") still gave rigged ships for the bigger types.
This one asks for a museum model with its masts taken out. Appends parts to tools/make_parts.py's PARTS at run time."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tools"))
import make_parts  # noqa: E402

MUSEUM = ("a museum scale model of the hull of {what}, shown with its masts taken out for restoration: the deck is open, "
          "with only round holes where the masts would stand. There are no masts, no spars, no yards, no sails, no ropes "
          "and no flags anywhere; the hull and deck alone")
make_parts.PARTS.update({
    "hull2_brig": MUSEUM.format(what="an 18th-century brig of war, a dark blue hull with a single row of eight gun ports "
                                     "with red lids, an ochre stripe, a carved scroll at the bow and a square stern with five "
                                     "windows; a raised quarterdeck at the stern and a short forecastle"),
    "hull2_galleon": MUSEUM.format(what="a 17th-century galleon with a tall forecastle and a high carved sterncastle with "
                                        "gilded galleries and stern windows, two rows of gun ports, a deep red hull with black "
                                        "wales and gold trim"),
    "hull2_sloop": MUSEUM.format(what="an 18th-century sloop of war, a long low hull painted black with a broad yellow band "
                                      "along the gun ports, six gun ports a side, a flush deck with a wheel and a small "
                                      "skylight, a long straight bow"),
})
sys.argv = ["make_parts.py", "--parts", "hull2_brig", "hull2_galleon", "hull2_sloop", "--seeds", "11", "22", "33", "44"]
make_parts.main()
