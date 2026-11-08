# The conformance set

What every rasterizer in this repository is held to: `drawings/` (compiled SDGs), `expected/` (what the reference,
vexelray-fields' `SdgRender`, draws of them), and `cases.txt` (one case a line: drawing, width, height, originX,
originY, unitsPerPixel, expected picture). The repository's README says how to read a case.

The art is in `generate/src/main/resources/art`: six of the VexelRay suite's marks, four SDG 1.2 group cases (a group
opacity over an overlap, a clip, a luminance mask, and all three nested), and a picture traced into colour regions for
sampled paints. `generate/` remakes everything here from it:

    mvn -f conformance/generate compile exec:java

A change to the reference, or to the art, is a change to every expected picture: remake them, and say why in the commit.
