# SDG rasterizers

**SDG, signed distance graphics**, is a drawing format for art that stays sharp at any size: layers painted back to
front, each layer's shape a distance field (an SDF, or an MSDF for sharp corners) and its colour a paint (one colour,
or a texture), and since 1.2 groups with an opacity, a clip or a mask, drawn apart and laid over once. What SVG is to
paths, SDG is to distance fields. The format is specified in vexelray-fields'
`docs/sdg.md` (version 1.2, on its `v0.2` branch); its reference renderer is vexelray-fields' `SdgRender` (Java).

This repository holds **rasterizers for SDG in other languages**, one a folder, and the **conformance set** every one
of them is held to.

| Folder | What it is | State |
| --- | --- | --- |
| [`c/`](c) | A rasterizer in C11, no dependencies: reads a compiled drawing (`.sdgb`), PNG pages and all, and draws it on the CPU by the spec's §2, groups included | All 33 conformance cases bit-exact (worst difference 0); tiled and threaded, and still byte for byte the plain transcription at every size tried (`c/tests/fast.c`) |
| [`shaders/`](shaders) | SDG's GPU shaders written once in [SupirVast](https://github.com/sibarum/supirvast)'s core IR, emitted as SPIR-V (validated), GLSL 450, GLSL ES 300, HLSL and Metal | Generated and validated; not yet run against the conformance set on a GPU |
| [`conformance/`](conformance) | Drawings, the views to draw them at, and what the reference draws: the test every rasterizer here passes | 10 drawings (marks, groups, clips, masks, a traced picture), 3 views each, 1 traced |

## The conformance set

`conformance/cases.txt` lists one case a line:

```
drawing.sdgb width height originX originY unitsPerPixel expected.png
```

Draw the drawing `width × height` pixels, pixel `(i, j)` sampling the canvas at
`(originX + (i + 0.5) · unitsPerPixel, originY + (j + 0.5) · unitsPerPixel)`, straight RGBA8, on transparent; then compare
with `expected.png`, which `SdgRender` drew. A CPU rasterizer that follows the spec's arithmetic lands on it exactly; a
GPU one, whose texture filtering weighs texels in fixed point, within a few steps of 255.

Each drawing is drawn fitted into 64 × 64, fitted into 200 × 150 (not square), and a corner magnified eight times, where
filtering between texels shows. `conformance/generate` remakes the set from its art with vexelray-fields:

```
mvn -f conformance/generate compile exec:java
```

## Building

```
c/build.cmd                                   # Windows: MSVC, through Visual Studio's environment and CMake
cmake -S c -B c/build && cmake --build c/build && ctest --test-dir c/build      # anywhere else
mvn -f shaders/generator compile exec:java    # the shaders, into shaders/out (needs the SupirVast artifacts)
```

## Speed

The C rasterizer draws the reference's pixels, byte for byte, but not in the reference's order of work. Each speed-up
is one the arithmetic allows, so none needs a tolerance:

- **Taps once a row and a column.** Which texels a pixel reads, and how much of each, depends along x on its column
  only and along y on its row, so a layer works them out once each, not once a pixel.
- **Tiles in or out.** A bilinear read lies between the least and the greatest of its four texels (and an MSDF's
  median between the medians of the least and of the greatest). An 8 × 8 tile whose texels are all far enough outside
  a shape is covered exactly 0 and skipped; one whose texels are all far enough inside is covered exactly 1, and its
  distance is not read.
- **Bands of rows on threads.** `sdg_render_threaded` cuts the picture into bands, each drawn by whichever thread is
  free; a band computes its pixels as the whole would. `sdg_render` is one thread.

`c/tests/fast.c` holds `src/render.c` to `tests/reference.c` (the first, literal version) on every drawing: fitted at
17 to 1024 pixels, not square, zoomed into a corner, the middle and past an edge, themed, on 1 and 7 threads.
`c/build/sdgbench` times it. Medians on a 20-thread desktop, MSVC `/O2`:

| Drawing, fitted | Literal, 1 thread | `render.c`, 1 thread | `render.c`, 20 threads |
| --- | ---: | ---: | ---: |
| mainframe, 64 px | 0.42 ms | 0.14 ms | 0.14 ms (one band) |
| mainframe, 256 px | 6.4 ms | 1.2 ms | 0.43 ms |
| mainframe, 1024 px | 103 ms | 16 ms | 1.8 ms |
| background icon, 1024 px | 129 ms | 24 ms | 3.2 ms |
| VexelRay logo, 1024 px | 79 ms | 12 ms | 1.4 ms |

vexelray-fields' `docs/comparison.md` sets these against SVG (imagelib, native, and a Java rasteriser) and PNG.

## Adding a language

A folder of its own, a rasterizer that reads `.sdgb` (or the XML `.sdg`) and a test that runs `conformance/cases.txt`.
The spec's §2 is the algorithm; `c/tests/reference.c` is a short, literal reading of it, and `c/src/render.c` the
same arithmetic with the work it can skip skipped (see [Speed](#speed)).

## License

Source code is licensed under the Apache License, Version 2.0 ([`LICENSE`](LICENSE)); documentation and other text
content under Creative Commons Attribution 4.0 International ([`LICENSE-docs`](LICENSE-docs)). See [`NOTICE`](NOTICE).
