// Weed Farmer 64 — vertical slice 1: the grow room.
#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>
#include <math.h>

#define FB_COUNT 3

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

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, FB_COUNT, GAMMA_NONE,
                 FILTERS_RESAMPLE_ANTIALIAS_DEDITHER);
    joypad_init();
    rdpq_init();
    t3d_init((T3DInitParams){});
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

    for (;;) {
        joypad_poll();
        joypad_inputs_t jin = joypad_get_inputs(JOYPAD_PORT_1);
#ifdef AUTOTEST
        {   // ares JS input injection is unreliable on libdragon joypad_poll;
            // synthesize: every 150 frames press d_right, every 200 press A.
            int f = (int)(t * 60.0f);
            jin.btn.d_right = (f % 150) < 4;
            jin.btn.d_left  = false;
            jin.btn.a       = (f % 200) < 4;
            jin.stick_x = 0; jin.stick_y = 0;
        }
#endif
        frameIdx = (frameIdx + 1) % FB_COUNT;
        t += 0.016f;

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

        int night = dayT >= 40.0f;   // light phase done -> growth pauses

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
                debugf("[wf64] harvested pot %d q=%.2f $%d (tot $%d)\n",
                       sel + 1, pots[sel].quality, val, money);
                pots[sel] = (Pot){ .pos = pots[sel].pos, .growth = 0.02f,
                                   .water = 0.8f, .quality = 0.8f, .rot = 0.0f };
            } else {
                pots[sel].water = 1.0f;
            }
        }

        // day cycle: 60 s per day (40 s light, 20 s night)
        dayT += 0.016f;
        if (dayT > 60.0f) { dayT = 0.0f; day++; }

        t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(65.0f), 5.0f, 300.0f);
        t3d_viewport_look_at(&viewport, &camPos, &camTarget, &(fm_vec3_t){{0, 1, 0}});

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
        if (pots[sel].water < 0.25f && pots[sel].growth < 1.0f)
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 8, 190, "THIRSTY! press A");

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
