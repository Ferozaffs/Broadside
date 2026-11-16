// The Shipwright's parts: figureheads, stern pieces, the flag and the pennant. All built here from simple shapes (tubes
// swept along curves, lathed and squashed spheres), so there's no model to license. Each comes in its own frame, in
// metres for a frigate: figureheads face +z (out over the bow), stern pieces face +z too (they're turned round to
// look aft), the flag and pennant hang from their hoist at the origin and fly along +x.
#pragma once
#include "raylib.h"

namespace parts {

enum PartMesh {
    PM_SERPENT, PM_SERPENT_EYES,  // a sea serpent rearing over the bow (and its eyes, in red)
    PM_SCROLL,                    // a gilded scroll, curled like a fiddle's head
    PM_HORN, PM_HORN_COLLAR,      // a narwhal's horn for a ram (ivory), and its gilded collar
    PM_SKULL, PM_SKULL_DARK,      // a skull (bone), and its eyes and nose (black)
    PM_BONES,                     // two crossed bones (behind the skull on the stern)
    PM_SUN,                       // a gilded sun with rays
    PM_FLAG,                      // 3:2 cloth, 4.2 x 2.8 m, canvas coordinates for the picture (texcoords2)
    PM_PENNANT,                   // a long tapering streamer
    PM_STAFF,                     // a pole, 1 m long along +y (scaled)
    PM_COUNT
};

void Build();  // (once, with a GL context)
const Mesh& Get(PartMesh m);
constexpr float kFlagW = 4.2f, kFlagH = 2.8f, kPennantL = 7.5f;

}  // namespace parts
