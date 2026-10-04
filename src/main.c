// Weed Farmer 64 — vertical slice 1: the grow room.
#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>
#include <t3d/tpx.h>
#include <math.h>

#define FB_COUNT 3

#define MAXPART 64
typedef struct {
    fm_vec3_t pos, vel;
    int life;
    uint8_t col[4];
    int size;          // <10 = mote (floats, no gravity)
} Particle;
static Particle parts[MAXPART];
static int partHead = 0;
static TPXParticleS8 *tpxBuf;
static T3DMat4FP *tpxMat;

static void fx_burst(fm_vec3_t at, int count, int up, uint8_t col[4])
{
    for (int i = 0; i < count; i++) {
        Particle *p = &parts[partHead];
        partHead = (partHead + 1) % MAXPART;
        float a = (float)i / count * 6.28318f;
        p->pos = (fm_vec3_t){{ at.v[0] + cosf(a) * 2.0f,
                              at.v[1] + 10.0f + (up ? 6.0f : 0.0f),
                              at.v[2] + sinf(a) * 2.0f }};
        p->vel = (fm_vec3_t){{ cosf(a) * 0.10f, up ? 0.35f : 0.05f, sinf(a) * 0.10f }};
        p->life = 30 + (i % 8) * 2;
        p->col[0] = col[0]; p->col[1] = col[1]; p->col[2] = col[2]; p->col[3] = 0xFF;
        p->size = up ? 28 : 18;
    }
}

typedef struct {
    fm_vec3_t pos;
    float growth;      // 0..1
    float water;       // 0..1
    float quality;     // 0..1 running avg of water while growing
    float rot;
} Pot;

int main(void)
{
    debug_init_isviewer();
    debug_init_usblog();
    asset_init_compression(2);
    dfs_init(DFS_DEFAULT_LOCATION);

#ifdef AUDIO_ON   // -DAUDIO_ON: SFX enabled; ares-headless COP2 bug kills audio+tpx overlay combo
    audio_init(22050, 0.03f);
    mixer_init(8);
    mixer_set_vol(0.8f);
    wav64_t sndWater, sndHarvest, sndThirst;
    wav64_open(&sndWater,   "rom:/sfx/water.wav64");
    wav64_open(&sndHarvest, "rom:/sfx/harvest.wav64");
    wav64_open(&sndThirst,  "rom:/sfx/thirsty.wav64");
    int sfxCool = 0;
#endif

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, FB_COUNT, GAMMA_NONE,
                 FILTERS_RESAMPLE);
    joypad_init();
    rdpq_init();
    tpxBuf = malloc_uncached(sizeof(TPXParticleS8) * (MAXPART / 2));
    tpxMat = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(tpxMat,
        (float[3]){1,1,1}, (float[3]){0,0,0}, (float[3]){0,0,0});
    t3d_init((T3DInitParams){});
#ifdef PARTICLES   // -DPARTICLES: tinyPX fx; ares-headless COP2 crash under our overlay combo (see README)
    tpx_init((TPXInitParams){});
#endif
    rdpq_text_register_font(FONT_BUILTIN_DEBUG_MONO,
                           rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO));
    T3DViewport viewport = t3d_viewport_create_buffered(FB_COUNT);

    T3DMat4FP *matFP = malloc_uncached(sizeof(T3DMat4FP) * 17 * FB_COUNT);

    T3DModel *mPlant = t3d_model_load("rom:/plant.t3dm");
    T3DModel *mSprout = t3d_model_load("rom:/sprout.t3dm");
    T3DModel *mPot   = t3d_model_load("rom:/pot.t3dm");
    T3DModel *mLamp  = t3d_model_load("rom:/lamp.t3dm");

    {
        // force lit+flat combiner on generated models (dummy materials)
        T3DModel *mm[3] = { mPlant, mPot, mSprout };
        const char *mn[3] = { "plantMat", "potMat", "sproutMat" };
        for (int k = 0; k < 3; k++) {
            T3DMaterial *mat = t3d_model_get_material(mm[k], mn[k]);
            if (mat) {
                mat->renderFlags |= T3D_FLAG_SHADED;
                mat->colorCombiner = RDPQ_COMBINER_SHADE;
                debugf("[ab] forced SHADED on %s\n", mat->name);
            }
        }
    }

    Pot pots[6];
    for (int i = 0; i < 6; i++) {
        pots[i] = (Pot){
            .pos = {{ -36.0f + (i % 3) * 36.0f, 0.0f, -14.0f + (i / 3) * 26.0f }},
            .growth = 0.05f + (i % 3) * 0.45f,
            .water = 0.5f,
            .quality = 0.5f,
            .rot = 0.0f,
        };
    }

    // Grow lights: two purple point lights over the rows (xyz, strength, color).
    struct { fm_vec3_t pos; float strength; color_t color; } lights[2] = {
        { {{-24, 38, 0}}, 150.0f, {0xC8, 0x60, 0xFF, 0xFF} },
        { {{ 24, 38, 0}}, 150.0f, {0xFF, 0x40, 0xC0, 0xFF} },
    };

    debugf("[wf64] plant aabb min %d %d %d max %d %d %d\n",
           mPlant->aabbMin[0], mPlant->aabbMin[1], mPlant->aabbMin[2],
           mPlant->aabbMax[0], mPlant->aabbMax[1], mPlant->aabbMax[2]);
    debugf("[wf64] pot aabb min %d %d %d max %d %d %d\n",
           mPot->aabbMin[0], mPot->aabbMin[1], mPot->aabbMin[2],
           mPot->aabbMax[0], mPot->aabbMax[1], mPot->aabbMax[2]);
    debugf("[wf64] lamp aabb min %d %d %d max %d %d %d\n",
           mLamp->aabbMin[0], mLamp->aabbMin[1], mLamp->aabbMin[2],
           mLamp->aabbMax[0], mLamp->aabbMax[1], mLamp->aabbMax[2]);

    int money = 0;           // dollars from sold buds
    int harvested = 0;       // total buds cut
    int sel = 2;             // player-selected pot
    uint32_t prevDir = 0;    // edge detection for d-pad/stick cycling
    int day = 1;
    float dayT = 0.0f;
#ifdef NIGHT_AT_BOOT
    dayT = 45.0f;   // debug: start in night phase for screenshots
#endif

    fm_vec3_t camPos = {{0, 22, 104}};
    fm_vec3_t camTarget = {{0, 16, -8}};
    float t = 0.0f;
    int frameIdx = 0;

    rspq_block_begin();
        t3d_model_draw(mPot);
    rspq_block_t *dplPot = rspq_block_end();
    rspq_block_begin();
        t3d_model_draw(mLamp);
    rspq_block_t *dplLamp = rspq_block_end();

#ifdef FX_AT_BOOT
    fx_burst((fm_vec3_t){{-36.0f, 14.0f, 12.0f}}, 16, 0, (uint8_t[4]){80, 140, 255, 0xFF});
#endif
    int screen = 0;   // 0 = title, 1 = play
    float camA = 0.6f;
    for (;;) {
        joypad_poll();
        joypad_inputs_t jin = joypad_get_inputs(JOYPAD_PORT_1);
        joypad_buttons_t jp = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        if (screen == 0 && (jin.btn.start || jin.btn.a)) screen = 1;
#ifdef AUTOTEST
        {   // ares JS input injection is unreliable on libdragon joypad_poll;
            // synthesize: every 150 frames press d_right, every 200 press A.
            int f = (int)(t * 60.0f);
            jin.btn.d_right = (f % 150) < 4;
            jin.btn.d_left  = false;
            jin.btn.a       = (f % 200) < 4;
            jin.btn.start = (f == 200);
            jin.stick_x = 0; jin.stick_y = 0;
        }
#endif
        frameIdx = (frameIdx + 1) % FB_COUNT;
        t += 0.016f;

        int night = dayT >= 40.0f;   // hoisted: HUD reads it even on title
        if (screen == 0) goto render;
        // camera drifts a hair with the stick for feel
        camPos.v[0] += jin.stick_x * -0.04f;
        if (camPos.v[0] > 30) camPos.v[0] = 30;
        if (camPos.v[0] < -30) camPos.v[0] = -30;

        // d-pad left/right (or stick edges) cycle the selected pot
        uint32_t dir = 0;
        if (jin.btn.d_right || jin.stick_x >  40) dir = 1;
        if (jin.btn.d_left  || jin.stick_x < -40) dir = 2;
        if (dir && !(prevDir & dir)) sel = (sel + (dir == 1 ? 1 : 5)) % 6;
        prevDir = dir;

        // (night computed above)

        // plants grow slowly (the hum of the grow-op)
        for (int i = 0; i < 6; i++) {
            if (!night && pots[i].growth < 1.0f) {
                pots[i].growth += 0.0003f * (0.3f + pots[i].water);
                if (pots[i].growth > 1.0f) pots[i].growth = 1.0f;
                // quality tracks how well-watered the grow was
                pots[i].quality += (pots[i].water - pots[i].quality) * 0.02f;
            }
            pots[i].water -= night ? 0.00002f : 0.00008f;
            if (pots[i].water < 0.0f) pots[i].water = 0.0f;
            pots[i].rot = sinf(t * 0.8f + i) * 0.05f;  // fan breeze sway
        }
        // A: water if thirsty/growing, harvest if ripe
        if (jin.btn.a) {
            if (pots[sel].growth >= 1.0f) {
                int val = 10 + (int)(pots[sel].quality * 40.0f);
                money += val;
                harvested++;
                fx_burst(pots[sel].pos, 24, 1, (uint8_t[4]){220, 90, 255, 0xFF});
#ifdef AUDIO_ON
                if (!mixer_ch_playing(7)) mixer_ch_play(7, &sndHarvest.wave);
#endif
                debugf("[wf64] harvested pot %d q=%.2f $%d (tot $%d)\n",
                       sel + 1, pots[sel].quality, val, money);
                pots[sel] = (Pot){ .pos = pots[sel].pos, .growth = 0.02f,
                                   .water = 0.8f, .quality = 0.8f, .rot = 0.0f };
            } else {
                pots[sel].water = 1.0f;
                fx_burst(pots[sel].pos, 16, 0, (uint8_t[4]){80, 140, 255, 0xFF});
#ifdef AUDIO_ON
                if (!mixer_ch_playing(6)) mixer_ch_play(6, &sndWater.wave);
#endif
            }
        }

        // day cycle: 60 s per day (40 s light, 20 s night)
        dayT += 0.016f;
        if (dayT > 60.0f) { dayT = 0.0f; day++; }

        // ambient grow-room motes (also keeps tpx drawing every frame = stable)
        {
            static int mote = 0;
            if (++mote % 10 == 0) {
                Particle *p = &parts[partHead];
                partHead = (partHead + 1) % MAXPART;
                float a = t * 0.9f;
                p->pos = (fm_vec3_t){{ sinf(a * 1.7f) * 55.0f,
                                      6.0f + fmodf(t * 3.0f, 30.0f),
                                      cosf(a * 1.3f) * 24.0f }};
                p->vel = (fm_vec3_t){{ 0.01f, 0.03f, 0.0f }};
                p->life = 90;
                p->col[0] = 200; p->col[1] = 120; p->col[2] = 255; p->col[3] = 0xFF;
                p->size = 8;
            }
        }
        for (int i = 0; i < MAXPART; i++) {
            if (parts[i].life <= 0) continue;
            parts[i].life--;
            parts[i].pos.v[0] += parts[i].vel.v[0];
            parts[i].pos.v[1] += parts[i].vel.v[1];
            parts[i].pos.v[2] += parts[i].vel.v[2];
            if (parts[i].size >= 10) parts[i].vel.v[1] -= 0.012f;   // motes float
            if (parts[i].pos.v[1] < 0.0f) parts[i].life = 0;
        }

#ifdef AUDIO_ON
        if (sfxCool > 0) sfxCool--;
#endif
        render:
        t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(65.0f), 5.0f, 300.0f);
        if (screen == 0) {
            camA += 0.004f;
            fm_vec3_t orbit = (fm_vec3_t){{ sinf(camA) * 115.0f, 34.0f, cosf(camA) * 115.0f + 6.0f }};
            t3d_viewport_look_at(&viewport, &orbit, &(fm_vec3_t){{0, 12, -6}}, &(fm_vec3_t){{0, 1, 0}});
        } else {
            t3d_viewport_look_at(&viewport, &camPos, &camTarget, &(fm_vec3_t){{0, 1, 0}});
        }

        int mi = 0;
        for (int i = 0; i < 6; i++) {
            float s, rot = pots[i].rot;
            // stage pop: sprout uses its own model + bigger relative scale;
            // ripe plants wobble harder (harvest-ready tell)
            if (pots[i].growth < 0.35f) {
                s = 0.30f + pots[i].growth * 0.55f;             // sprout ~1/4 pot height
            } else {
                s = 0.05f + pots[i].growth * 0.04f;              // plant 192u tall
            }
            if (pots[i].growth >= 1.0f) rot *= 4.0f;
            // plant / sprout
            t3d_mat4fp_from_srt_euler(&matFP[mi + 17 * frameIdx],
                (float[3]){s, s, s},
                (float[3]){rot, rot * 1.3f + t * 0.1f, rot},
                (float[3]){pots[i].pos.v[0], pots[i].pos.v[1] + 6.5f, pots[i].pos.v[2]});
            mi++;
            // pot
            t3d_mat4fp_from_srt_euler(&matFP[mi + 17 * frameIdx],
                (float[3]){0.16f, 0.16f, 0.16f},
                (float[3]){0, 0, 0},
                (float[3]){pots[i].pos.v[0], pots[i].pos.v[1], pots[i].pos.v[2]});
            mi++;
        }

        rdpq_attach(display_get(), display_get_zbuf());
        t3d_frame_start();
        t3d_viewport_attach(&viewport);
        t3d_screen_clear_color(RGBA32(18, 8, 30, 0xFF));
        t3d_screen_clear_depth();

        if (night) {
            t3d_light_set_ambient((uint8_t[4]){8, 8, 22, 0xFF});
            t3d_light_set_point(0, (uint8_t[4]){80, 90, 200, 0xFF},
                                &(fm_vec3_t){{0, 50, 40}}, 60.0f, false);
            t3d_light_set_count(1);
        } else {
            t3d_light_set_ambient((uint8_t[4]){22, 10, 34, 0xFF});
            for (int i = 0; i < 2; i++)
                t3d_light_set_point(i, &lights[i].color.r, &lights[i].pos, lights[i].strength, false);
            t3d_light_set_count(2);
        }

        // pots + plants (6 each, mats packed 0..11).
        // push one stack slot, then repeated set(true) = the example-03 idiom;
        // without the push, every set(true) accumulates on the view base and
        // models fly off to infinity (black frame).
        t3d_matrix_push_pos(1);
        mi = 0;
        for (int i = 0; i < 6; i++) {
            t3d_matrix_set(&matFP[mi + 17 * frameIdx], true);
            rspq_block_run(dplPot);
            mi++;
            t3d_matrix_set(&matFP[mi + 17 * frameIdx], true);
            if (pots[i].growth < 0.35f)
                t3d_model_draw(mSprout);   // stage 0: seedling
            else
                t3d_model_draw(mPlant);    // veg + flower
            mi++;
        }
        // floor: crate box flattened to a slab
        {
            T3DMat4FP *mfp = &matFP[14 + 17 * frameIdx];
            t3d_mat4fp_from_srt_euler(mfp,
                (float[3]){1.6f, 0.015f, 0.9f},
                (float[3]){0, 0, 0},
                (float[3]){0, -2.6f, 8.0f});
            t3d_matrix_set(mfp, true);
            rspq_block_run(dplPot);
        }
        // back wall
        {
            T3DMat4FP *mfp = &matFP[15 + 17 * frameIdx];
            t3d_mat4fp_from_srt_euler(mfp,
                (float[3]){1.6f, 0.9f, 0.015f},
                (float[3]){0, 0, 0},
                (float[3]){0, 18.0f, -26.0f});
            t3d_matrix_set(mfp, true);
            rspq_block_run(dplPot);
        }
        // lamp bars over the rows
        for (int i = 0; i < 2; i++) {
            T3DMat4FP *mfp = &matFP[12 + i + 17 * frameIdx];
            t3d_mat4fp_from_srt_euler(mfp,
                (float[3]){0.5f, 0.12f, 0.12f},
                (float[3]){0, 0, 0},
                (float[3]){lights[i].pos.v[0], lights[i].pos.v[1], lights[i].pos.v[2]});
            t3d_matrix_set(mfp, true);
            rspq_block_run(dplLamp);
        }
        // selection cursor: bobbing lamp-bar chip over the selected pot
        {
            float bob = sinf(t * 4.0f) * 2.0f;
            T3DMat4FP *mfp = &matFP[16 + 17 * frameIdx];
            t3d_mat4fp_from_srt_euler(mfp,
                (float[3]){0.22f, 0.22f, 0.22f},
                (float[3]){sinf(t * 3.0f) * 0.4f, t * 2.0f, 0},
                (float[3]){pots[sel].pos.v[0], pots[sel].pos.v[1] + 27.0f + bob,
                           pots[sel].pos.v[2] + 6.0f});
            t3d_matrix_set(mfp, true);
            rspq_block_run(dplLamp);
        }
        t3d_matrix_pop(1);

        // ---- particles (tinyPX) ----
#ifdef PARTICLES
        {
            TPXParticleS8 *pb = tpxBuf;
            int live = 0;
            for (int i = 0; i < MAXPART; i++) {
                if (parts[i].life <= 0) continue;
                TPXParticleS8 *pr = &pb[live >> 1];
                int8_t *base = (int8_t *)pr;
                uint8_t *cb = (uint8_t *)pr;
                if (!(live & 1)) {   // particle A: pos bytes 0..3, color 8..11
                    base[0] = (int8_t)parts[i].pos.v[0];
                    base[1] = (int8_t)parts[i].pos.v[1];
                    base[2] = (int8_t)parts[i].pos.v[2];
                    base[3] = (int8_t)parts[i].size;
                    cb[8]  = parts[i].col[0];
                    cb[9]  = parts[i].col[1];
                    cb[10] = parts[i].col[2];
                    cb[11] = 0xFF;
                } else {             // particle B: pos bytes 4..7, color 12..15
                    base[4] = (int8_t)parts[i].pos.v[0];
                    base[5] = (int8_t)parts[i].pos.v[1];
                    base[6] = (int8_t)parts[i].pos.v[2];
                    base[7] = (int8_t)parts[i].size;
                    cb[12] = parts[i].col[0];
                    cb[13] = parts[i].col[1];
                    cb[14] = parts[i].col[2];
                    cb[15] = 0xFF;
                }
                live++;
            }
            if (live & 1) {   // even count required
                TPXParticleS8 *pr = &pb[live >> 1];
                pr->posB[0] = pr->posB[1] = pr->posB[2] = 0; pr->sizeB = 0;
                pr->colorB[0] = pr->colorB[1] = pr->colorB[2] = pr->colorB[3] = 0;
                live++;
            }
            if (live) {
                rdpq_set_mode_standard();
                rdpq_mode_zbuf(true, true);
                rdpq_mode_zoverride(true, 0, 0);
                rdpq_mode_combiner(RDPQ_COMBINER1((PRIM,0,ENV,0), (0,0,0,1)));
                tpx_state_from_t3d();
                // RSP reads this via DMA — must be uncached (cached static =
                // random-frame COP2/timer crashes, example 18 uses malloc_uncached)
                tpx_matrix_push(tpxMat);
                tpx_state_set_base_size(64);
                tpx_state_set_scale(1.0f, 1.0f);
                tpx_particle_draw_s8(pb, live);
            }
        }
#endif

        // ---- title overlay / HUD ----
        if (screen == 0) {
            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_set_prim_color(RGBA32(0, 0, 0, 170));
            rdpq_fill_rectangle(34, 66, 286, 148);
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 66, 78, "WEED FARMER 64");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 42, 98, "  a grow-room simulator  ");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 42, 112, "  d-pad: aim  a: water/cut  ");
            if (((int)(t * 2.0f)) & 1)
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 100, 130, "PRESS START");
        }
        if (screen == 1) {
        // ---- HUD (2D) ----
        rdpq_set_mode_standard();
        // bottom bar backdrop
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_set_prim_color(RGBA32(12, 6, 22, 220));
        rdpq_fill_rectangle(0, 204, 320, 240);
        // water bar
        rdpq_set_prim_color(RGBA32(40, 120, 200, 255));
        rdpq_fill_rectangle(60, 230, 60 + (int)(100.0f * pots[sel].water), 238);
        // growth bar
        rdpq_set_prim_color(RGBA32(80, 200, 90, 255));
        rdpq_fill_rectangle(200, 230, 200 + (int)(100.0f * pots[sel].growth), 238);
        // ready marker
        if (pots[sel].growth >= 1.0f) {
            rdpq_set_prim_color(RGBA32(255, 80, 220, 255));
            rdpq_fill_rectangle(304, 230, 312, 238);
        }
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 8, 208,
            "POT %d/6 %s  $$%d  CUT %d",
            sel + 1, night ? "NIGHT" : "DAY " , money, harvested);
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 8, 196, "DAY %d", day);
        if (pots[sel].growth >= 1.0f)
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 120, 190, "RIPE! A=HARVEST");
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 60, 219, "WATER");
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 219, "GROW");
        if (pots[sel].water < 0.25f && pots[sel].growth < 1.0f) {
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 8, 190, "THIRSTY! press A");
#ifdef AUDIO_ON
            if (sfxCool <= 0) { mixer_ch_play(5, &sndThirst.wave); sfxCool = 240; }
#endif
        }

        }   // screen == 1
        rdpq_detach_show();

        if ((int)(t * 60) % 180 == 0) {
            static char ln[200];
            snprintf(ln, sizeof ln,
                "[wf64] t=%d d=%d night=%d pots: g0=%.2f w0=%.2f sel=%d gsel=%.2f $%d cut%d",
                (int)(t * 60), day, night, pots[0].growth, pots[0].water, sel,
                pots[sel].growth, money, harvested);
            debugf("%s\n", ln);
        }
    }
}
