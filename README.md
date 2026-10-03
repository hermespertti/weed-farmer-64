# Weed Farmer 64 🌱

A cel-shaded grow-room farming game for the Nintendo 64, built on
[tiny3d](https://github.com/HailToDodongo/tiny3d) — a from-scratch 3D RSP
microcode and C API on top of [libdragon](https://github.com/DragonMinded/libdragon)
`preview`.

![Box art](docs/boxart.png)

## Screenshot

The grow room: six procedural low-poly plants in a row, purple grow-light
point lights, floor + back wall, rendered through the software RDP
(angrylion) at 320×240 in the ares emulator.

![Grow room](docs/growroom.png)

## Current state

- [x] tiny3d + libdragon `preview` toolchain in an isolated SDK prefix
- [x] Procedural pot + plant models (Blender one-shot headless, vertex colors)
- [x] glTF → `.t3dm` import pipeline (`--ignore-materials` + runtime combiner patch)
- [x] Grow room renders: 6 pots, growing plants, dual purple point lights, floor, walls, lamp bars
- [x] Headless screenshot verification (ares-test + angrylion RDP)
- [ ] Growth stage swaps (sprout → veg → flower)
- [ ] Harvest minigame + HUD
- [ ] Roadside stand economy
- [ ] Real hardware timing

## Gotchas learned

- `--ignore-materials` in the glTF importer emits dummy materials **lacking
  `T3D_FLAG_SHADED`** — the RSP then skips lighting entirely and everything
  renders black. Fix: force `renderFlags |= T3D_FLAG_SHADED` and
  `colorCombiner = RDPQ_COMBINER_SHADE` via `t3d_model_get_material()` at load.
- The exporter needs a material on every mesh, or the importer silently emits
  an empty model.
- Multi-object draws need one `t3d_matrix_push_pos(1)` / `t3d_matrix_pop(1)`
  around the loop; without it every `t3d_matrix_set(m, true)` accumulates on
  the previous transform and objects fly to infinity.
- Box art generated with sd-cpp + Qwen-Image-2.1 (Vulkan backend — no CUDA in
  this build).

## Building

```sh
export N64_INST=/path/to/t3d-sdk N64_GCCPREFIX=/usr
make
```

Requires libdragon `preview` installed to `$N64_INST` plus tiny3d built
against it (`t3d.mk` in the prefix).

## Controls

| Input | Action |
|---|---|
| Control stick | Pan camera |
| A | Water selected pot |

## Release

Playable ROM on the [releases page](../../releases).
