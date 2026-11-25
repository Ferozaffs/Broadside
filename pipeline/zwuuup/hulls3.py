"""Third batch of bare hulls (2026-11-21): a derelict ghost ship (the Flying Dutchman's class), and the galleon again
with stronger wording (the museum wording still gave galleons their rig)."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tools"))
import make_parts  # noqa: E402

MUSEUM = ("a museum scale model of the hull of {what}, shown with its masts taken out for restoration: the deck is open, "
          "with only round holes where the masts would stand. There are no masts, no spars, no yards, no sails, no ropes "
          "and no flags anywhere; the hull and deck alone")
make_parts.PARTS.update({
    "hull3_ghost": MUSEUM.format(what="a derelict haunted frigate: rotting pale grey-green timbers with a few gaping holes "
                                      "in the planking, broken railings, barnacles and strands of hanging seaweed, a single "
                                      "row of gun ports glowing faint green, a weathered carved figurehead of a weeping woman"),
    "hull3_galleon": ("only the lower hull of a 17th-century Spanish galleon carved as one solid block of wood, like a "
                      "shipwright's half-finished model: a tall forecastle and a high carved sterncastle with gilded galleries "
                      "and stern windows, two rows of gun ports, deep red paint with black wales and gold trim. Absolutely "
                      "nothing above the deck: no masts, no sticks, no rigging, no sails, no flags"),
})
sys.argv = ["make_parts.py", "--parts", "hull3_ghost", "hull3_galleon", "--seeds", "11", "22", "33", "44"]
make_parts.main()
