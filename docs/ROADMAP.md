# Weed Farmer 64 — Roadmap / TODO

_State as of 2026-10-05 ~19:00 EEST. Everything marked ✅ is shipped, pushed
to `hermespertti/weed-farmer-64` master, and soak-verified headless (ares +
angrylion). Release channel: `v0-slice1`._

## ✅ Shipped
- Title screen, grow-room scene, day/night cycle (40 s light phase)
- 6-pot sim: watering, growth stages, quality-by-water, harvest payouts
- Roadside Stand shop: Grow Light / Drip Irrigation / Thick Bags upgrades
- Strain system: Sttlime (free) + Blueberry / JRK / Gelato (purchasable),
  grow/value/thirst tradeoffs, strain-colored harvest FX, HUD strain tags
- Seed selection L/R (skips unowned), empty-pot replant flow
- Pause / how-to overlay (Z) with live strain cheat-sheet
- Best-day record + EEPROM save v3 (strains, owned mask, seed pick, best day)
- Third-person walk mode: stick tank controls, chase cam on open (+z) side,
  C-right toggle, lantern light, step bob/lean
- Particles (`-DPARTICLES`), audio + XM music (`-DAUDIO_ON`)
- Demo video `wf64-demo2.mp4` on release (kept OUT of git)

## 🔥 Next up (in rough priority order)
1. **Farmer palette** — glTF exports COLOR_0 as u16 (5123), tiny3d importer
   drops it → farmer renders lit-gray. Test FLAT primColor on the FIXED
   build workflow (touch + md5-verified ROMs), or bake colors as a tiny
   texture / region-split materials (hat/shirt/overalls separate meshes).
2. **Walk-cam collision** — farmer currently clips through pots/wall;
   add simple radius push-out + camera collision ray so he can't leave frame.
3. **Curing / drying minigame** — post-harvest: hang buds, timing minigame,
   quality multiplier → bigger payouts. Natural fit for day cycle.
4. **Watering can capacity** — walk to sink/tap to refill; adds movement
   purpose to walk mode.
5. **More room / second grow tent** — unlockable after $500; new lighting
   color presets per tent.
6. **Achievements / toasts** — first harvest, $100, best-day beats, all
   strains owned.
7. **Slight weather / humitionity stat** — humidity bar that drifts, fans
   upgrade counters it.

## ⚠️ Hardware-pending (headless can't verify)
- Audio + particles combo: stochastic COP2 Unusable (PC:800005FC) in ares's
  angrylion/VU interpreter; same ROM sometimes runs 2500 frames clean.
  Needs real-hardware A/B. `-DPARTICLES -DAUDIO_ON` combo ROM.
- EEPROM cross-boot persistence (ares writes no saves; mupen --test-single
  wrote none). Header savetype is eeprom4k, likely fine on metal.
- All timing numbers: ares ms ≠ hardware ms (interpreter tax).

## 🛠 Workflow gotchas (so future-me doesn't lose an evening)
- **ALWAYS `touch src/main.c` + `md5sum` before A/B captures** — changing
  `EXTRA_CFLAGS` alone does NOT invalidate main.o; two flag-variant ROMs can
  be byte-identical (this cost a whole night chasing phantom invisibility).
- Screenshots are **640×240**; vision-crop coords must use that, never 1920.
- `grep -c` exit-1 on zero breaks `&&` chains; use `;` + artifact checks.
- Debug hooks: `-DAUTOSTART -DAUTOTEST` (synth inputs + `[at] synth active`
  marker), `-DFAROFF`, `-DSHOWONLYFARMER`, `-DFX_AT_BOOT`, `-DSEEDSHOP`.
- Headless Blender: `env -i HOME=/home/lex PATH=/usr/bin:/bin
  /usr/sbin/blender --background --python assets/gen_farmer.py`.
- glTF exporter already converts Z-up→Y-up: never pre-bake 90° rotation.
- Verification: `ares-test test/shotwalk.js <rom> <out.png> --timeout 280`;
  a soak is only valid with `[at] synth active` in the log.
- Videos live on the GitHub release, never in git (`*.mp4` gitignored).

## 💡 Someday
- N64 controller rumble hook (if Controller Pak path in libdragon preview).
- Local 2-player: co-op watering vs. competition mode (2nd controller).
- Sound: tracker modules for night vs day (only db_key.xm wired now).

## 🎮 Next platforms (user interest)
- **SNES** — ideas: falling-sand port (sand64 physics fits SNES CPU nicely),
  farming/cell-shade parody on SA-1, or a Mandelbrot fractal zoomer (SVP chip).
  Toolchains: cc65 vs PVS-DSL vs SGDK-alikes; libdragon equivalent = toholo/TONC-era.
- **GBA** — ideas: Weed Farmer GBA (tiny 240×160 grow-room), falling-sand
  GBA (ARM7 TDMA palette tricks), homebrew carts flash easily.
  Toolchains: devkitARM + libgba / maxmod audio; headless: mGBA (has
  scripting + save testing built in — better test story than ares).
