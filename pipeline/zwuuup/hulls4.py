"""Fourth hull batch (2026-11-21): the ghost ship again. hulls3 s22 was the right ship (a derelict frigate hull,
green-lit ports, seaweed, a carved head at the bow) but failed the frame checks (it touched the edge; a heavy floor
shadow). Same brief, framed smaller, with the background and light spelled out."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tools"))
import make_parts  # noqa: E402

make_parts.PARTS.update({
    "hull4_ghost": ("a museum scale model of the hull of a derelict haunted frigate, shown with its masts taken out for "
                    "restoration: the deck is open, with only round holes where the masts would stand. Rotting pale "
                    "grey-green timbers, broken railings, barnacles and strands of hanging seaweed, a single row of round "
                    "gun ports glowing faint green, a high stern gallery, a weathered carved figurehead at the bow. There "
                    "are no masts, no spars, no sails, no ropes and no flags. The whole model is small in the picture with "
                    "wide empty space all round it, floating on a plain flat light-grey background, soft even light, no "
                    "floor and no shadow"),
})
sys.argv = ["make_parts.py", "--parts", "hull4_ghost", "--seeds", "55", "66", "77", "88", "99", "111"]
make_parts.main()
