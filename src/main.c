// Weed Farmer 64 — vertical slice 1: the grow room.
#include <libdragon.h>
#include <eeprom.h>
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
typedef struct {
    uint32_t magic;
    int16_t day, lvLight, lvIrrig, lvBags;
    int32_t money, harvested;
    float growMul;
    struct { float growth, water, quality; } pot[6];
    uint32_t crc;
    int8_t strain[6];    // per-pot planted strain (-1 empty)
    uint8_t owned;       // bit i = strain i seeds bought at the stand
    int8_t gSeedSel;     // last chosen seed
    uint8_t pad[2];
    int32_t bestDay;     // longest day survived
} SaveGame;
#define SAVE_MAGIC 0x57463603u   // WF64 v3 (strain ownership)
static uint32_t save_crc(const SaveGame *s)
{
    const uint8_t *p = (const uint8_t *)s;
    uint32_t c = 0x811C9DC5u;
    for (uint32_t i = 0; i < offsetof(SaveGame, crc); i++) {
        c ^= p[i];
        c *= 0x01000193u;
    }
    return c;
}

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
        p->size = up ? 52 : 36;   // bigger quads: 18-28 were invisible murk at room scale
    }
}

typedef struct {
    fm_vec3_t pos;
    float growth;      // 0..1
    float water;       // 0..1
    float quality;     // 0..1 running avg of water while growing
    float rot;
    int8_t strain;     // -1 = empty pot, else index into strains[]
    int8_t seedSel;    // transient: which strain the A button will plant
} Pot;

// ---- strains: speed x value x thirst tradeoffs ----
typedef struct {
    const char *name;
    float growMul;    // growth rate multiplier
    float valMul;     // harvest value multiplier
    float thirst;     // water drain multiplier
    uint8_t col[4];   // bud color for the tell
} Strain;
static const Strain strains[4] = {
    { "SATTLIME",  1.00f, 1.00f, 1.00f, {200, 96, 255, 255} },  // baseline
    { "BLUEBERRY", 0.75f, 1.60f, 1.15f, { 90, 120, 255, 255} },  // slow rich
    { "JRK",       1.40f, 0.70f, 1.35f, {255, 210,  80, 255} },  // fast junk
    { "GELATO",    1.10f, 1.25f, 0.75f, {255, 130, 170, 255} },  // thrifty sweet
};

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
    xm64player_t music;
    xm64player_open(&music, "rom:/music/db_key.xm64");
    xm64player_set_loop(&music, true);
    xm64player_play(&music, 0);          // mixer ch0-3; SFX live on ch5-7
    debugf("[music] playing %dch\n", xm64player_num_channels(&music));
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

    T3DMat4FP *matFP = malloc_uncached(sizeof(T3DMat4FP) * 18 * FB_COUNT);

    T3DModel *mPlant = t3d_model_load("rom:/plant.t3dm");
    T3DModel *mSprout = t3d_model_load("rom:/sprout.t3dm");
    T3DModel *mPot   = t3d_model_load("rom:/pot.t3dm");
    T3DModel *mLamp  = t3d_model_load("rom:/lamp.t3dm");
    T3DModel *mFarmer = t3d_model_load("rom:/farmer.t3dm");

    {
        // force lit+flat combiner on generated models (dummy materials)
        T3DModel *mm[4] = { mPlant, mPot, mSprout, mFarmer };
        const char *mn[4] = { "plantMat", "potMat", "sproutMat", "farmerMat" };
        for (int k = 0; k < 4; k++) {
            T3DMaterial *mat = t3d_model_get_material(mm[k], mn[k]);
            if (mat) {
                mat->renderFlags |= T3D_FLAG_SHADED;
                mat->colorCombiner = RDPQ_COMBINER_SHADE;
                if (k == 3) {   // farmer: FLAT combiner + hi-vis material color
                    mat->colorCombiner = RDPQ_COMBINER_FLAT;
                    mat->primColor = RGBA32(255, 230, 40, 255);   // unmistakable yellow body
                }
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
            .strain = (i % 3 == 2) ? -1 : (int8_t)(i % 4),  // pot 3 & 6 start empty-ish
            .seedSel = 0,
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
    debugf("[wf64] farmer aabb min %d %d %d max %d %d %d\n",
           mFarmer->aabbMin[0], mFarmer->aabbMin[1], mFarmer->aabbMin[2],
           mFarmer->aabbMax[0], mFarmer->aabbMax[1], mFarmer->aabbMax[2]);
    debugf("[wf64] lamp aabb min %d %d %d max %d %d %d\n",
           mLamp->aabbMin[0], mLamp->aabbMin[1], mLamp->aabbMin[2],
           mLamp->aabbMax[0], mLamp->aabbMax[1], mLamp->aabbMax[2]);

    int money = 0;           // dollars from sold buds
    int harvested = 0;       // total buds cut
    int bestDay = 0;         // longest day survived (EEPROM-persisted)
    int8_t gSeedSel = 0;     // currently selected strain to plant
    uint8_t ownedMask = 0x01;  // SATTLIME free; others bought at the stand
    int sel = 2;             // player-selected pot
    uint32_t prevDir = 0;    // edge detection for d-pad/stick cycling
    int day = 1;
    float dayT = 0.0f;
#ifdef NIGHT_AT_BOOT
    dayT = 45.0f;   // debug: start in night phase for screenshots
#endif

    fm_vec3_t camPos = {{0, 22, 104}};
    fm_vec3_t camTarget = {{0, 16, -8}};
    // third-person farmer: pos, facing yaw, walk bob phase, mode
    int canAct = 1;   // proximity gate for A (always open in menu-cam)
    fm_vec3_t farPos = {{0, -2.0f, 60.0f}};   // start near the front wall
    float farYaw = 3.14159f;                  // face the pots (-Z is "into" room)
    float walkPh = 0.0f;
    float camOrbit = 0.0f;   // C buttons swing the chase cam
#ifdef FORCE_MENUCAM
    int walkMode = 0;
#else
    int walkMode = 1;
#endif    // 1 = third-person walk; 0 = classic menu-cam (C toggles)
    float t = 0.0f;
    int frameIdx = 0;

    rspq_block_begin();
        t3d_model_draw(mPot);
    rspq_block_t *dplPot = rspq_block_end();
    rspq_block_begin();
        t3d_model_draw(mLamp);
    rspq_block_t *dplLamp = rspq_block_end();
    rspq_block_begin();
        t3d_model_draw(mFarmer);
    rspq_block_t *dplFarmer = rspq_block_end();

#ifdef FX_AT_BOOT
    fx_burst((fm_vec3_t){{36.0f, 12.0f, 12.0f}}, 20, 1, (uint8_t[4]){120, 200, 255, 0xFF});
    fx_burst((fm_vec3_t){{28.0f, 20.0f, 10.0f}}, 20, 1, (uint8_t[4]){160, 220, 255, 0xFF});
    fx_burst((fm_vec3_t){{44.0f, 16.0f, 14.0f}}, 20, 1, (uint8_t[4]){80, 180, 255, 0xFF});
    // int8 size field: >127 wraps negative and the quads vanish — 90 is the fat max
    for (int i = 0; i < MAXPART; i++) if (parts[i].life > 0) { parts[i].size = 90; parts[i].life = 600; parts[i].vel.v[1] = 0.22f; }
#endif
    int screen = 0;   // 0 = title, 1 = play, 2 = shop
#ifdef AUTOSTART
    screen = 1;   // skip title so fx physics runs for shots
#endif
#ifdef SEEDSHOP
    screen = 2;   // shop open at boot for UI shots
#endif
#ifdef CAM_TITLE_FRONT
    float camA = 0.0f;   // deterministic: face the pots for fx shots
#else
    float camA = 0.6f;
#endif
#ifdef AUTOSTART
    int screen0_play = 1;
#endif
    int shopSel = 0;
    int lvLight = 0, lvIrrig = 0, lvBags = 0;
    float growMul = 1.0f;

    // ---- EEPROM save load (4Kbit = 128B = 16 blocks) ----
    bool haveSave = false;
    if (eeprom_present() != EEPROM_NONE && eeprom_total_blocks() >= 16) {
        SaveGame sg;
        uint8_t *dst = (uint8_t *)&sg;
        for (int b = 0; b < (int)sizeof(SaveGame) / EEPROM_BLOCK_SIZE; b++)
            eeprom_read(b, dst + b * EEPROM_BLOCK_SIZE);
        if (sg.magic == SAVE_MAGIC && sg.crc == save_crc(&sg)) {
            day = sg.day; money = sg.money; harvested = sg.harvested;
            lvLight = sg.lvLight; lvIrrig = sg.lvIrrig; lvBags = sg.lvBags;
            growMul = sg.growMul;
            for (int i = 0; i < 6; i++) {
                pots[i].growth = sg.pot[i].growth;
                pots[i].water = sg.pot[i].water;
                pots[i].quality = sg.pot[i].quality;
                pots[i].strain = sg.strain[i];
            }
            if (sg.gSeedSel >= 0 && sg.gSeedSel < 4) gSeedSel = sg.gSeedSel;
            bestDay = sg.bestDay;
            ownedMask = sg.owned ? sg.owned : 0x01;
            haveSave = true;
            debugf("[save] loaded day%d $%d L%d I%d B%d best%d\n", day, money, lvLight, lvIrrig, lvBags, bestDay);
        } else {
            debugf("[save] none/corrupt\n");
        }
    } else {
        debugf("[save] no eeprom\n");
    }
    int saveDirty = 0;   // frames since last save-relevant action

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
            jin.btn.start = ((f >= 200) && (f % 400) == 0);  // open/close shop visits
            jin.btn.l     = ((f % 300) == 100);               // seed cycle
            // edge-only Z: one-frame press at f950 (after shop closes at 900).
            // pause stays open until soak ends; shot captures it any time after.
            jin.btn.z     = (f == 950);
            jin.btn.b   = (f == 900) || (f == 1700);
            jin.stick_x = 0; jin.stick_y = 0;
            // autopilot: park the farmer beside a cycling pot (sel follows by proximity)
            {   int wp = (f / 480) % 6;
                farPos.v[0] = pots[wp].pos.v[0] + 14.0f;
                farPos.v[2] = pots[wp].pos.v[2] + 14.0f;
            }
            static bool told = false;
            if (!told) { told = true; debugf("[at] synth active\n"); }
        }
#endif
        if (screen == 0 && (jin.btn.start || jin.btn.a)) screen = 1;
        static bool prevStart = false, prevB = false;
        bool eStart = jin.btn.start && !prevStart;
        bool eB     = jin.btn.b && !prevB;
        prevStart = jin.btn.start; prevB = jin.btn.b;
        if (screen == 1 && eStart) { screen = 2; debugf("[ui] OPEN shop\n"); }
        else if (screen == 2 && (eB || eStart)) { screen = 1; debugf("[ui] CLOSE shop\n"); }
        static bool prevZ = false;
        bool eZ = jin.btn.z && !prevZ; prevZ = jin.btn.z;
        if (screen == 1 && eZ) screen = 3;          // pause + how-to
        else if (screen == 3 && (eZ || eB)) screen = 1;
        if (screen == 2) {
            uint32_t dn = 0;
            if (jin.btn.d_up   || jin.stick_y >  40) dn = 1;
            if (jin.btn.d_down || jin.stick_y < -40) dn = 2;
            static uint32_t pshop = 0;
            if (dn && !(pshop & dn)) shopSel = (shopSel + (dn == 1 ? 2 : 1)) % 6;
            pshop = dn;
            static bool pA = false;
            if (jin.btn.a && !pA) {
                static const int cost[6] = {40, 70, 30, 50, 35, 60};
                if (shopSel < 3) {
                    int maxed = (shopSel == 0 && lvLight >= 3) ||
                                (shopSel == 1 && lvIrrig >= 2) ||
                                (shopSel == 2 && lvBags >= 3);
                    if (!maxed && money >= cost[shopSel]) {
                        money -= cost[shopSel];
                        if (shopSel == 0) { lvLight++; growMul = 1.0f + 0.5f * lvLight; }
                        if (shopSel == 1) { lvIrrig++; }
                        if (shopSel == 2) { lvBags++; }
                        debugf("[shop] bought %d tot $%d (L%d I%d B%d)\n", shopSel, money, lvLight, lvIrrig, lvBags);
                        saveDirty = 1;
                    }
                } else {
                    int bit = 1 << (shopSel - 2);   // strain index = row-2
                    if (!(ownedMask & bit) && money >= cost[shopSel]) {
                        money -= cost[shopSel];
                        ownedMask |= bit;
                        debugf("[shop] seeds strain %d owned %02x tot $%d\n", shopSel - 2, ownedMask, money);
                        saveDirty = 1;
                    }
                }
            }
            pA = jin.btn.a;
        }
        frameIdx = (frameIdx + 1) % FB_COUNT;
        t += 0.016f;

        int night = dayT >= 40.0f;   // hoisted: HUD reads it even on title
        if (screen == 0) goto render;
        if (screen == 3) goto render;  // paused: no growth, no day tick, no walk
        if (!walkMode) {
            // menu-cam: stick drift + d-pad pot cycling (classic controls)
            // (lights keep the saturated grow-room mood)
            camPos.v[0] += jin.stick_x * -0.04f;
            if (camPos.v[0] > 30) camPos.v[0] = 30;
            if (camPos.v[0] < -30) camPos.v[0] = -30;
            uint32_t dir = 0;
            if (jin.btn.d_right || jin.stick_x >  40) dir = 1;
            if (jin.btn.d_left  || jin.stick_x < -40) dir = 2;
            if (dir && !(prevDir & dir)) sel = (sel + (dir == 1 ? 1 : 5)) % 6;
            prevDir = dir;
            canAct = 1;
        }

        // (night computed above)

        // plants grow slowly (the hum of the grow-op)
        for (int i = 0; i < 6; i++) {
            if (!night && pots[i].growth < 1.0f) {
                pots[i].growth += 0.0003f * (0.3f + pots[i].water) * growMul
                    * (pots[i].strain >= 0 ? strains[pots[i].strain].growMul : 1.0f);
                if (pots[i].growth > 1.0f) pots[i].growth = 1.0f;
                // quality tracks how well-watered the grow was
                pots[i].quality += (pots[i].water - pots[i].quality) * 0.02f;
            }
            pots[i].water -= (night ? 0.00002f : 0.00008f) * (1.0f - 0.35f * lvIrrig)
                * (pots[i].strain >= 0 ? strains[pots[i].strain].thirst : 0.4f);
            if (pots[i].water < 0.0f) pots[i].water = 0.0f;
            pots[i].rot = sinf(t * 0.8f + i) * 0.05f;  // fan breeze sway
        }
        // A: water if thirsty/growing, harvest if ripe (walk mode: near the pot)
        if (jin.btn.a && canAct) {
            if (pots[sel].strain < 0) {
                // empty pot: plant the selected strain
                pots[sel].strain = gSeedSel;
                pots[sel].growth = 0.02f;
                pots[sel].water = 0.8f;
                pots[sel].quality = 0.8f;
                fx_burst(pots[sel].pos, 14, 0, (uint8_t *)strains[pots[sel].strain].col);
                debugf("[wf64] planted pot %d %s\n", sel + 1, strains[pots[sel].strain].name);
                saveDirty = 1;
            } else if (pots[sel].growth >= 1.0f) {
                const Strain *sp = &strains[pots[sel].strain];
                int val = (int)((10 + pots[sel].quality * 40.0f) * (1.0f + 0.25f * lvBags) * sp->valMul);
                money += val;
                harvested++;
                fx_burst(pots[sel].pos, 24, 1, (uint8_t *)sp->col);
#ifdef AUDIO_ON
                if (!mixer_ch_playing(7)) mixer_ch_play(7, &sndHarvest.wave);
#endif
                debugf("[wf64] harvested pot %d %s q=%.2f $%d (tot $%d)\n",
                       sel + 1, sp->name, pots[sel].quality, val, money);
                // pot goes EMPTY - player must plant (d-up/d-down picks strain)
                pots[sel].strain = -1;
                pots[sel].growth = 0.0f;
                pots[sel].water = 0.3f;
                pots[sel].quality = 0.5f;
                saveDirty = 1;
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
        if (dayT > 60.0f) { dayT = 0.0f; day++; if (day > bestDay) bestDay = day; saveDirty = 1; }

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
        // C toggles camera mode: third-person walk vs classic menu-cam
        static bool prevC = false;
        if (jin.btn.c_right && !prevC) { walkMode = !walkMode; debugf("[cam] mode %d\n", walkMode); }
        prevC = jin.btn.c_right;
        if (jin.btn.c_up)    camOrbit += 0.045f;   // hold C-up/C-down: orbit cam
        if (jin.btn.c_down)  camOrbit -= 0.045f;
        // L/R cycle the global seed choice; empty pots get it on A
        {   static bool prevL = false, prevR = false;
            if (jin.btn.l && !prevL) for (int k = 0; k < 4; k++) { gSeedSel = (gSeedSel + 3) % 4; if (ownedMask & (1 << gSeedSel)) break; }
            if (jin.btn.r && !prevR) for (int k = 0; k < 4; k++) { gSeedSel = (gSeedSel + 1) % 4; if (ownedMask & (1 << gSeedSel)) break; }
            prevL = jin.btn.l; prevR = jin.btn.r;
        }

        // ---- third-person farmer movement (tank controls) ----
        // facingDir(yaw) = (sin yaw, cos yaw); yaw=0 faces +Z.
        // stick maps to world dirs: up = -Z (toward the pots), right = +X.
        if (walkMode && screen == 1) {
            float mx = jin.stick_x * 0.0022f;
            float mz = -jin.stick_y * 0.0022f;
            if (jin.btn.d_left)  mx = -0.5f;
            if (jin.btn.d_right) mx =  0.5f;
            if (jin.btn.d_up)    mz = -0.5f;
            if (jin.btn.d_down)  mz =  0.5f;
            float mag = sqrtf(mx*mx + mz*mz);
            if (mag > 0.06f) {
                if (mag > 1.0f) { mx /= mag; mz /= mag; mag = 1.0f; }
                float spd = 0.45f * mag;
                farPos.v[0] += mx * spd;
                farPos.v[2] += mz * spd;
                if (farPos.v[0] >  70) farPos.v[0] =  70;
                if (farPos.v[0] < -70) farPos.v[0] = -70;
                if (farPos.v[2] >  66) farPos.v[2] =  66;
                if (farPos.v[2] < -44) farPos.v[2] = -44;
                farYaw = atan2f(mx, mz);        // face travel dir
                walkPh += mag * 0.35f;
            } else {
                walkPh *= 0.9f;
            }
            // selection = nearest pot to the farmer
            int bi = 0; float bd = 1e9f;
            for (int i = 0; i < 6; i++) {
                float dx = pots[i].pos.v[0] - farPos.v[0];
                float dz = pots[i].pos.v[2] - farPos.v[2];
                float d = dx*dx + dz*dz;
                if (d < bd) { bd = d; bi = i; }
            }
            sel = bi;
            canAct = (bd < 26.0f * 26.0f);
            // over-shoulder chase: close, raised, slightly offset right.
            // long distances fling the cam across the tiny room and other
            // pots occlude the farmer — keep it short and high.
            float camYaw = farYaw + camOrbit;
            // tight over-shoulder: cam just behind the shoulder, gaze at hip depth.
            // in a 90-unit room a distant cam only ever shows walls.
            // the room only has a BACK wall (+z); the front is open.
            // so the chase cam rides the open side: farmer always framed
            // against the room interior, never clipped by geometry.
            float orb = camOrbit * 0.5f;   // C-right swings the viewing angle a bit
            // high front view over the open side: full room, both pot rows,
            // farmer visible among them (classic fixed grow-room angle).
            camPos.v[0] += ((farPos.v[0] * 0.5f + 18.0f * sinf(camOrbit)) - camPos.v[0]) * 0.10f;
            camPos.v[1] += (58.0f - camPos.v[1]) * 0.10f;
            camPos.v[2] += (78.0f - camPos.v[2]) * 0.10f;
            camTarget.v[0] += (0.0f - camTarget.v[0]) * 0.20f;
            camTarget.v[1] += (2.0f - camTarget.v[1]) * 0.20f;
            camTarget.v[2] += (-2.0f - camTarget.v[2]) * 0.20f;
            static int mdbg = 0;
            if ((mdbg++ % 120) == 0)
                debugf("[far] pos %d %d yaw %d sel %d act %d cam %d %d %d tgt %d %d %d\n",
                       (int)farPos.v[0], (int)farPos.v[2], (int)(farYaw * 57.3f), sel, canAct,
                       (int)camPos.v[0], (int)camPos.v[1], (int)camPos.v[2],
                       (int)camTarget.v[0], (int)camTarget.v[1], (int)camTarget.v[2]);
        }

        render:
        t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(65.0f), 5.0f, 300.0f);
#ifdef SHOWONLYFARMER
        farYaw = 0.0f; farPos = (fm_vec3_t){{0, -2.0f, 10.0f}}; walkPh = 0;
        camPos = (fm_vec3_t){{26, 34, 58}};
        camTarget = (fm_vec3_t){{0, 12, 0}};
#endif
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
            t3d_mat4fp_from_srt_euler(&matFP[mi + 18 * frameIdx],
                (float[3]){s, s, s},
                (float[3]){rot, rot * 1.3f + t * 0.1f, rot},
                (float[3]){pots[i].pos.v[0], pots[i].pos.v[1] + 6.5f, pots[i].pos.v[2]});
            mi++;
            // pot
            t3d_mat4fp_from_srt_euler(&matFP[mi + 18 * frameIdx],
                (float[3]){0.16f, 0.16f, 0.16f},
                (float[3]){0, 0, 0},
                (float[3]){pots[i].pos.v[0], pots[i].pos.v[1], pots[i].pos.v[2]});
            mi++;
        }

        rdpq_attach(display_get(), display_get_zbuf());
        t3d_frame_start();
        t3d_viewport_attach(&viewport);
#ifdef SHOWONLYFARMER
        t3d_screen_clear_color(RGBA32(128, 128, 128, 0xFF));
#else
        t3d_screen_clear_color(RGBA32(18, 8, 30, 0xFF));
#endif
        t3d_screen_clear_depth();

        if (night) {
            t3d_light_set_ambient((uint8_t[4]){8, 8, 22, 0xFF});
            t3d_light_set_point(0, (uint8_t[4]){80, 90, 200, 0xFF},
                                &(fm_vec3_t){{0, 50, 40}}, 60.0f, false);
            t3d_light_set_count(1);
        } else {
            t3d_light_set_ambient((uint8_t[4]){110, 70, 150, 0xFF});  // walk mode readability
            for (int i = 0; i < 2; i++)
                t3d_light_set_point(i, &lights[i].color.r, &lights[i].pos, lights[i].strength, false);
            t3d_light_set_count(2);
        }
        // walk mode: lantern riding with the farmer so he (and the view)
        // aren't swallowed by darkness outside the grow-light pool
        if (walkMode && screen == 1) {
            fm_vec3_t lantern = {{farPos.v[0] + sinf(farYaw) * 8.0f,
                                 farPos.v[1] + 34.0f, farPos.v[2] + cosf(farYaw) * 8.0f}};
            t3d_light_set_point(2, (uint8_t[4]){255, 252, 240, 0xFF}, &lantern, 380.0f, false);  // near-white so avatar colors read
            t3d_light_set_count(3);
        }

        // pots + plants (6 each, mats packed 0..11).
        // push one stack slot, then repeated set(true) = the example-03 idiom;
        // without the push, every set(true) accumulates on the view base and
        // models fly off to infinity (black frame).
        t3d_matrix_push_pos(1);
        mi = 0;
#ifndef SHOWONLYFARMER
        for (int i = 0; i < 6; i++) {
            t3d_matrix_set(&matFP[mi + 18 * frameIdx], true);
            rspq_block_run(dplPot);
            mi++;
            t3d_matrix_set(&matFP[mi + 18 * frameIdx], true);
            if (pots[i].growth < 0.35f)
                t3d_model_draw(mSprout);   // stage 0: seedling
            else
                t3d_model_draw(mPlant);    // veg + flower
            mi++;
        }
        // floor: crate box flattened to a slab
        {
            T3DMat4FP *mfp = &matFP[14 + 18 * frameIdx];
            t3d_mat4fp_from_srt_euler(mfp,
                (float[3]){1.6f, 0.015f, 0.9f},
                (float[3]){0, 0, 0},
                (float[3]){0, -2.6f, 8.0f});
            t3d_matrix_set(mfp, true);
            rspq_block_run(dplPot);
        }
        // back wall
        {
            T3DMat4FP *mfp = &matFP[15 + 18 * frameIdx];
            t3d_mat4fp_from_srt_euler(mfp,
                (float[3]){1.6f, 0.9f, 0.015f},
                (float[3]){0, 0, 0},
                (float[3]){0, 18.0f, -26.0f});
            t3d_matrix_set(mfp, true);
            rspq_block_run(dplPot);
        }
        // lamp bars over the rows
        for (int i = 0; i < 2; i++) {
            T3DMat4FP *mfp = &matFP[12 + i + 18 * frameIdx];
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
            T3DMat4FP *mfp = &matFP[16 + 18 * frameIdx];
            t3d_mat4fp_from_srt_euler(mfp,
                (float[3]){0.22f, 0.22f, 0.22f},
                (float[3]){sinf(t * 3.0f) * 0.4f, t * 2.0f, 0},
                (float[3]){pots[sel].pos.v[0], pots[sel].pos.v[1] + 27.0f + bob,
                           pots[sel].pos.v[2] + 6.0f});
            t3d_matrix_set(mfp, true);
            rspq_block_run(dplLamp);
        }
#endif
        // farmer (third-person avatar, slot 17)
        {
            float bob = sinf(walkPh) * 1.4f;                 // step bounce
            float lean = sinf(walkPh) * 0.06f;               // weight shift
            T3DMat4FP *mfp = &matFP[17 + 18 * frameIdx];
            t3d_mat4fp_from_srt_euler(mfp,
                (float[3]){0.0063f, 0.0063f, 0.0063f},         // 6048u raw -> 38u tall (~2.5x pots)
                (float[3]){lean, farYaw, lean * 0.6f},
                (float[3]){farPos.v[0], farPos.v[1] + bob, farPos.v[2]});
            t3d_matrix_set(mfp, true);
            {   // engine-space screen projection of his feet + hat top
                T3DVec3 s;
                t3d_viewport_calc_viewspace_pos(&viewport, &s, (T3DVec3*)&farPos);
                T3DVec3 h = {{farPos.v[0], farPos.v[1] + 38.0f, farPos.v[2]}};
                t3d_viewport_calc_viewspace_pos(&viewport, &h, &h);
                static int sdbg = 0;
                if ((sdbg++ % 120) == 0)
                    debugf("[scr] feet %d %d hat %d %d\n",
                        (int)s.v[0], (int)s.v[1], (int)h.v[0], (int)h.v[1]);
            }
#ifndef FAROFF
            rspq_block_run(dplFarmer);
#endif
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
#ifdef TPXPROBE
            static int pcount = 0;
            if ((pcount++) % 30 == 0 && live) {
                debugf("[tpx] live=%d p0=(%d,%d,%d sz=%d)\n", live,
                       (int)parts[0].pos.v[0], (int)parts[0].pos.v[1], (int)parts[0].pos.v[2], parts[0].size);
            }
#endif
            if (live) {
                rdpq_set_mode_standard();
                rdpq_mode_zbuf(true, true);
                rdpq_mode_zoverride(true, 0, 0);
                rdpq_mode_combiner(RDPQ_COMBINER1((PRIM,0,ENV,0), (0,0,0,1)));
                rdpq_set_env_color(RGBA32(255, 255, 255, 255));  // else prim x ENV = ~20% murk
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

        if (screen == 2) {
            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_set_prim_color(RGBA32(0, 0, 0, 200));
            rdpq_fill_rectangle(34, 40, 286, 190);
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 96, 48, "ROADSIDE STAND");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 58, "$$%d", money);
            static const char *nm[6] = { "GROW LIGHT +50%", "DRIP IRRIGATION", "THICK BAGS +25%",
                                          "BLUEBERRY SEEDS", "JRK SEEDS", "GELATO SEEDS" };
            static const int cost[6] = {40, 70, 30, 50, 35, 60};
            int lv[3] = {lvLight, lvIrrig, lvBags};
            int mx[3] = {3, 2, 3};
            // seeds: shop rows 3..5 map to strains 1..3 (0 is free baseline)
            int seedOwned[3] = { (ownedMask>>1)&1, (ownedMask>>2)&1, (ownedMask>>3)&1 };
            for (int i = 0; i < 6; i++) {
                int y = 74 + i * 18;
                if (i >= 3) {
                    rdpq_set_mode_standard();
                    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                    const uint8_t *sc = strains[i - 2].col;
                    rdpq_set_prim_color(RGBA32(sc[0], sc[1], sc[2], 255));
                    rdpq_fill_rectangle(44, y + 2, 50, y + 10);
                    rdpq_set_mode_standard();
                    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
                }
                if (i == shopSel) {
                    // text printf above switched rdpq to TEXT mode — must re-arm
                    // standard+FLAT or this fill renders through the text combiner (invisible)
                    rdpq_set_mode_standard();
                    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                    rdpq_set_prim_color(RGBA32(120, 60, 180, 255));
                    rdpq_fill_rectangle(44, y - 2, 276, y + 14);
                    rdpq_set_mode_standard();
                    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
                }
                if (i < 3) {
                    if (lv[i] >= mx[i])
                        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 54, y, "  %s   MAXED ", nm[i]);
                    else
                        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 54, y, "  %s $$%d Lv%d", nm[i], cost[i], lv[i]);
                } else {
                    if (seedOwned[i - 3])
                        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 54, y, "  %s  OWNED", nm[i]);
                    else
                        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 54, y, "  %s $$%d", nm[i], cost[i]);
                }
            }
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 44, 184, "A buy   START/B back");
        }
        // ---- EEPROM flush: debounced after save-relevant actions ----
        if (saveDirty > 0 && ++saveDirty > 180) {
            saveDirty = 0;
            SaveGame sg = {
                .magic = SAVE_MAGIC, .day = day,
                .lvLight = lvLight, .lvIrrig = lvIrrig, .lvBags = lvBags,
                .money = money, .harvested = harvested, .growMul = growMul,
            };
            for (int i = 0; i < 6; i++) {
                sg.pot[i].growth = pots[i].growth;
                sg.pot[i].water = pots[i].water;
                sg.pot[i].quality = pots[i].quality;
                sg.strain[i] = pots[i].strain;
            }
            sg.gSeedSel = gSeedSel;
            if (day > bestDay) bestDay = day;
            sg.bestDay = bestDay;
            sg.owned = ownedMask;
            sg.crc = save_crc(&sg);
            // async write; status byte is stale right after the call —
            // integrity is proven by magic+crc surviving the NEXT boot
            eeprom_write_bytes(&sg, 0, sizeof(SaveGame));
            debugf("[save] flush day%d $%d L%d\n", day, money, lvLight);
#ifdef SAVE_VERIFY
            {   // libdragon keeps an EEPROM cache refreshed by write; read
                // the full image back and re-check magic+crc
                SaveGame rb;
                eeprom_read_bytes(&rb, 0, sizeof(SaveGame));
                debugf("[save] verify %s (magic %08x crc %08x/%08x)\n",
                       (rb.magic == SAVE_MAGIC && rb.crc == save_crc(&rb)) ? "OK" : "BAD",
                       rb.magic, rb.crc, save_crc(&rb));
            }
#endif
        }

        // ---- title overlay / HUD ----
        if (screen == 0) {
            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_set_prim_color(RGBA32(0, 0, 0, 170));
            rdpq_fill_rectangle(34, 66, 286, 148);
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 66, 78, "WEED FARMER 64");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 42, 98, haveSave ? "  CONTINUE  d=%d  $$%d " : " a grow-room simulator ", day, money);
            if (bestDay > 0)
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 42, 106, "  best run: day %d        ", bestDay);
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 42, 112, "  d-pad: aim  a: water/cut  ");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 42, 124, "  L/R: pick seed  C: walk cam ");
            if (((int)(t * 2.0f)) & 1)
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 100, 130, "PRESS START");
        }
        if (screen == 3) {
            // ---- pause / how-to ----
            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_set_prim_color(RGBA32(0, 0, 0, 210));
            rdpq_fill_rectangle(20, 12, 300, 196);
            rdpq_set_prim_color(RGBA32(255, 230, 40, 255));
            rdpq_fill_rectangle(20, 12, 300, 15);
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 96, 22, "PAUSED");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 30, 38, " STICK  walk (third person)");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 30, 50, " d-pad select pot / strafe");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 30, 62, " A      water | plant | cut");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 30, 74, " L/R    pick seed strain");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 30, 86, " C      camera  Z unpause");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 30, 98, " START  roadside stand");
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 30, 114, " STRAINS   grow value thirst");
            for (int i = 0; i < 4; i++) {
                rdpq_set_prim_color(RGBA32(strains[i].col[0], strains[i].col[1], strains[i].col[2], 255));
                rdpq_fill_rectangle(30, 128 + i * 11, 36, 136 + i * 11);
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 42, 128 + i * 11,
                    " %-9s %4.0f%%  %4.0f%%  %4.0f%%", strains[i].name,
                    strains[i].growMul * 100.0f, strains[i].valMul * 100.0f, strains[i].thirst * 100.0f);
            }
            if (((int)(t * 2.0f)) & 1)
                rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 84, 182, "Z/B: BACK TO GROWING");
            goto render_done;
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
        if (pots[sel].growth >= 1.0f && pots[sel].strain >= 0) {
            const uint8_t *c = strains[pots[sel].strain].col;
            rdpq_set_prim_color(RGBA32(c[0], c[1], c[2], 255));
            rdpq_fill_rectangle(304, 230, 312, 238);
        }
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 8, 208,
            "POT %d/6 %s  $$%d  CUT %d",
            sel + 1, night ? "NIGHT" : "DAY " , money, harvested);
        if (pots[sel].strain >= 0)
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 120, 208, "[%s]", strains[pots[sel].strain].name);
        else
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 120, 208, "[EMPTY] L/R:seed A:plant");
        rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 8, 196, "DAY %d  BEST %d", day, bestDay);
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
        render_done:
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
