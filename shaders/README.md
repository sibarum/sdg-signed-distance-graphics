# SDG shaders

SDG's GPU shaders, written once in SupirVast's `core` IR (`generator/`), lowered to SPIR-V, checked by `spirv-val`, and
cross-compiled by SPIRV-Cross. `out/` holds every one in every form:

| File | Stage | What it draws |
| --- | --- | --- |
| `sdg_layer.vert.*` | vertex | a layer's quad, from `gl_VertexIndex` alone: draw 6 vertices, no vertex buffer |
| `sdg_layer.frag.*` | fragment | one shape layer, by the spec's §2 steps 1 to 5 |
| `sdg_group.vert.*` | vertex | a group's quad, the same way |
| `sdg_group.frag.*` | fragment | an SDG 1.2 group laid over: its target, its opacity, its mask |

Each as `.spv` (SPIR-V, Vulkan), `.glsl` (GLSL 450), `.es.glsl` (GLSL ES 300), `.hlsl` (HLSL, Direct3D) and `.metal`
(MSL). SupirVast does not name what it emits yet, so the sources read `_6._m2` and the like: the bindings and the
push-constant block below are the interface, by binding and by offset.

**State.** Generated and validated. Not yet drawn on a GPU against the conformance set: that host is the next piece.

## Drawing a drawing

1. Upload every page as an RGBA8 **UNORM** texture (never sRGB: half of them are distances), linear filtering, clamp to
   edge. Blend with `src = ONE, dst = ONE_MINUS_SRC_ALPHA`: the fragment shaders output premultiplied colour.
2. For each layer, back to front, draw 6 vertices with `sdg_layer.vert` + `sdg_layer.frag`, the stencil's page at
   binding 0, the paint's at binding 1 (the same texture again when the paint is a colour, or shares the page), and
   the push-constant block filled as below.
3. For a group with an opacity, a clip or a mask: draw its layers (by step 2) into a target of its own, cleared to
   transparent; draw its mask's layers into the same target beside them (or, for a clip, the clip stencil as a layer
   painted white, read as alpha); then draw the group's quad with `sdg_group.*`, the target at binding 0. A group with
   none of the three is only its layers. vexelray's canvas does exactly this (vexelray-fields `docs/sdg-groups.md`).

## The layer's push-constant block

Eight `vec4`, 128 bytes, at offsets 0, 16, ... 112. Canvas points are in the drawing's units, y down.

| # | Name | x | y | z | w |
| --- | --- | --- | --- | --- | --- |
| 0 | `clipRect` | the quad's left, in clip space | top | right | bottom |
| 1 | `canvasRect` | the quad's left, in canvas units | top | right | bottom |
| 2 | `stencilPlace` | the stencil's `originX` | `originY` | `scale` (texels a unit) | `range` (texels) |
| 3 | `stencilRect` | the stencil's `x` on its page | `y` | `width` | `height` |
| 4 | `stencilPage` | 1 / the page's width | 1 / its height | mode: the SDF's channel, 0 to 3, or 4 for an MSDF | units per pixel |
| 5 | `colour` | red | green | blue | alpha times the layer's opacity (straight, 0 to 1; white for a sampled paint) |
| 6 | `paintMap` | paint u = canvas x · this | paint v = canvas y · this | + this | + this; `x = 0`: a solid paint |
| 7 | `paintClamp` | the paint's rectangle in uv, half a texel in: u0 | v0 | u1 | v1 |

The quad is the stencil's rectangle on the canvas: `canvasRect = (originX, originY, originX + width / scale,
originY + height / scale)`, and `clipRect` the same rectangle through the view.

## The group's push-constant block

| # | Name | x | y | z | w |
| --- | --- | --- | --- | --- | --- |
| 0 | `clipRect` | the quad, in clip space | | | |
| 1 | `uvRect` | the group's rectangle of its target, in uv: u0 | v0 | u1 | v1 |
| 2 | `mask` | the mask's u offset from the group's, in the target | kind: 0 none, 1 luminance, 2 alpha | the group's opacity | — |
