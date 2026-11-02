/* JIMBO'S ROULETTE - a fan-made, single-player shotgun-roulette card-table game
 * for Nintendo 3DS / 2DS homebrew (devkitARM + libctru + citro2d).
 * All art is drawn in code. No external assets needed.
 *
 * Controls: D-Pad + A / B, or the touch screen. START quits.
 */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define MAXI 8
#define MAXS 8

enum { I_NONE, I_GLASS, I_BEER, I_CIG, I_SAW, I_CUFF, I_MED, I_PHONE, I_INV, I_ADR, I_COUNT };

static const char *INAME[]  = {"", "GLASS", "BEER", "CIGS", "SAW", "CUFFS", "MEDS", "PHONE", "INVERT", "ADREN"};
static const char *INAMEL[] = {"", "the magnifying glass", "a beer", "a cigarette", "the handsaw",
                               "the handcuffs", "expired medicine", "the burner phone",
                               "the inverter", "adrenaline"};
static const char *IDESC[]  = {"",
    "Look at the shell in the chamber.",
    "Eject the current shell.",
    "Heal 1 charge.",
    "Next live shot does double damage.",
    "Opponent skips their next turn.",
    "50%: +2 charges. 50%: -1 charge.",
    "Reveals a later shell in the chamber.",
    "Flip the current shell live <-> blank.",
    "Steal an item from the dealer and use it."};

enum { ST_TITLE, ST_MSG, ST_ADV, ST_LOAD, ST_NEWROUND, ST_PLAYER, ST_ADR_PICK,
       ST_DEALER, ST_DON, ST_GAMEOVER, ST_WIN };

typedef struct { int hp, max, items[MAXI], cuffed, sawed; } Actor;

static Actor A[2];
#define P A[0]
#define D A[1]

static int shells[MAXS], nshell, pos, nlive0, nblank0, bel_live, bel_blank;
static int known[2][MAXS];
static int turn, round_, run_, pot, st, nxt, timer, cur, adr_slot;
static char L1[96], L2[96], hint[80];
static int hint_t, dmood, dmood_t, flash_t, frame;

static u32 cBlack, cWhite, cRed, cBlue, cGray, cDark, cYellow, cBrown, cBG, cPanel;
static u32 ICOL[I_COUNT];
static C2D_TextBuf tbuf;

/* ------------------------------------------------------------ audio
 * Everything is synthesized in code at startup (no sound files needed) and
 * played through the DSP (ndsp). If the DSP firmware dump is missing
 * (sdmc:/3ds/dspfirm.cdc) the game simply runs silently.
 */
#define SRATE 22050
#define MRATE 11025
#define PI2   6.2831853f

enum { S_CLICK, S_BLAM, S_BLANK, S_ITEM, S_BEER, S_HEAL, S_BAD, S_SAW, S_CUFF, S_PHONE,
       S_INV, S_GLASS, S_ADR, S_SELECT, S_WIN, S_LOSE, S_LOAD, S_COUNT };

static const float SFX_DUR[S_COUNT] = {0.06f, 0.7f, 0.12f, 0.2f, 0.45f, 0.35f, 0.35f, 0.55f, 0.3f,
                                       0.35f, 0.35f, 0.35f, 0.55f, 0.04f, 0.9f, 1.1f, 0.5f};

static s16 *sfx_buf[S_COUNT];
static int sfx_len[S_COUNT];
static s16 *music_buf;
static int music_len;
static int audio_ok = 0, music_on = 1, music_duck = 0, sfx_next = 1;
static ndspWaveBuf wbs[8];
static u32 rng = 2463534242u;
static float lpf, ph_acc;

static float frand(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (rng & 0xFFFF) / 32768.0f - 1.0f;
}

static float sfx_sample(int id, float t, float d) {
    float x = 0, n = frand();
    switch (id) {
    case S_CLICK:  x = n * expf(-t * 120.f) * 0.6f; break;
    case S_SELECT: x = sinf(PI2 * 900.f * t) * expf(-t * 120.f) * 0.5f; break;
    case S_BLAM:
        lpf += 0.2f * (n - lpf);
        x = lpf * expf(-t * 7.f) * 1.8f
          + sinf(PI2 * (60.f + 90.f * expf(-t * 20.f)) * t) * expf(-t * 9.f) * 0.9f;
        break;
    case S_BLANK:
        x = n * expf(-t * 150.f) * 0.5f + sinf(PI2 * 150.f * t) * expf(-t * 40.f) * 0.4f;
        break;
    case S_ITEM: {
        float f = t < 0.08f ? 600.f : 900.f;
        x = (sinf(PI2 * f * t) > 0 ? 0.2f : -0.2f) * (1.f - t / d);
        break; }
    case S_BEER:
        lpf += 0.5f * (n - lpf);
        x = (n - lpf) * 0.6f * expf(-t * 5.f) * (0.6f + 0.4f * sinf(PI2 * 30.f * t))
          + sinf(PI2 * (200.f + 150.f * sinf(PI2 * 8.f * t)) * t) * 0.2f * expf(-t * 4.f);
        break;
    case S_HEAL: {
        static const float nt[4] = {523.25f, 659.25f, 783.99f, 1046.5f};
        int k = (int)(t / 0.07f); if (k > 3) k = 3;
        float lt = t - k * 0.07f;
        x = sinf(PI2 * nt[k] * lt) * 0.4f * expf(-lt * (k == 3 ? 6.f : 14.f));
        break; }
    case S_BAD: {
        float ph = 220.f * t - 75.f * t * t / d;
        float s = 2.f * (ph - floorf(ph)) - 1.f;
        x = s * 0.3f * (1.f - t / d);
        break; }
    case S_SAW: {
        float s = 2.f * (120.f * t - floorf(120.f * t)) - 1.f;
        float env = (t * 20.f < 1.f ? t * 20.f : 1.f) * (1.f - t / d);
        x = (n * 0.5f + s * 0.3f) * (0.6f + 0.4f * sinf(PI2 * 40.f * t)) * env * 0.9f;
        break; }
    case S_CUFF:
        x = sinf(PI2 * 2200.f * t) * expf(-t * 40.f) * 0.4f + n * expf(-t * 200.f) * 0.3f;
        if (t > 0.12f) {
            float lt = t - 0.12f;
            x += sinf(PI2 * 2800.f * lt) * expf(-lt * 35.f) * 0.4f + n * expf(-lt * 200.f) * 0.3f;
        }
        break;
    case S_PHONE:
        if (t < 0.1f || (t > 0.15f && t < 0.25f)) x = sinf(PI2 * 1400.f * t) * 0.3f;
        break;
    case S_INV: {
        float f = 300.f + 900.f * (1.f - fabsf(2.f * t / d - 1.f));
        ph_acc += PI2 * f / SRATE;
        x = sinf(ph_acc) * 0.35f * (1.f - t / d);
        break; }
    case S_GLASS:
        x = (sinf(PI2 * 1800.f * t) + 0.5f * sinf(PI2 * 2700.f * t)) * 0.2f
          * (1.f + 0.3f * sinf(PI2 * 12.f * t)) * sinf(3.14159f * t / d);
        break;
    case S_ADR:
        x = sinf(PI2 * 55.f * t) * expf(-t * 20.f) * 0.9f;
        if (t > 0.18f) { float lt = t - 0.18f; x += sinf(PI2 * 55.f * lt) * expf(-lt * 20.f) * 0.9f; }
        if (t > 0.30f) { float lt = t - 0.30f; x += sinf(PI2 * 1600.f * lt) * expf(-lt * 12.f) * 0.3f; }
        break;
    case S_WIN: {
        static const float nt[4] = {523.25f, 659.25f, 783.99f, 1046.5f};
        for (int k = 0; k < 4; k++) {
            float lt = t - k * 0.15f;
            if (lt >= 0) x += (sinf(PI2 * nt[k] * lt) + 0.3f * sinf(PI2 * nt[k] * 2.f * lt))
                              * expf(-lt * (k == 3 ? 4.f : 10.f)) * 0.25f;
        }
        break; }
    case S_LOSE: {
        static const float nt[4] = {392.f, 349.23f, 311.13f, 261.63f};
        for (int k = 0; k < 4; k++) {
            float lt = t - k * 0.25f;
            if (lt >= 0) x += (sinf(PI2 * nt[k] * lt) + 0.3f * sinf(PI2 * nt[k] * 2.f * lt))
                              * expf(-lt * (k == 3 ? 3.f : 6.f)) * 0.28f;
        }
        break; }
    case S_LOAD: {
        lpf += 0.3f * (n - lpf);
        float e1 = expf(-t * 45.f);
        float e2 = t > 0.22f ? expf(-(t - 0.22f) * 45.f) : 0.f;
        x = lpf * 1.5f * (e1 + e2) + sinf(PI2 * 1200.f * t) * 0.12f * e1;
        if (t > 0.22f) x += sinf(PI2 * 900.f * (t - 0.22f)) * 0.12f * e2;
        break; }
    }
    return x;
}

static void gen_sfx(int id) {
    float d = SFX_DUR[id];
    int n = (int)(d * SRATE);
    s16 *b = linearAlloc(n * sizeof(s16));
    if (!b) return;
    lpf = 0; ph_acc = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SRATE;
        float x = sfx_sample(id, t, d);
        if (t > d - 0.005f) x *= (d - t) / 0.005f;
        if (x > 1.f) x = 1.f; else if (x < -1.f) x = -1.f;
        b[i] = (s16)(x * 26000.f);
    }
    DSP_FlushDataCache(b, n * sizeof(s16));
    sfx_buf[id] = b; sfx_len[id] = n;
}

/* 8 bars of A-minor dread: pad chords, pulsing bass, clock-like ticks, music-box lead */
static void gen_music(void) {
    static const float chord[4][3] = {{220.f, 261.63f, 329.63f}, {174.61f, 220.f, 261.63f},
                                      {164.81f, 207.65f, 246.94f}, {146.83f, 174.61f, 220.f}};
    static const int   bar_chord[8] = {0, 0, 1, 2, 0, 0, 3, 2};
    static const float bassf[4] = {110.f, 87.31f, 82.41f, 73.42f};
    static const float acc[8] = {1.f, .4f, .7f, .4f, 1.f, .4f, .7f, .5f};
    static const float mel[8][4] = {
        {659.25f, 0, 523.25f, 0},       {440.f, 0, 0, 493.88f},
        {523.25f, 0, 440.f, 0},         {493.88f, 0, 415.3f, 0},
        {659.25f, 0, 523.25f, 0},       {587.33f, 0, 493.88f, 0},
        {698.46f, 0, 587.33f, 0},       {659.25f, 0, 415.3f, 493.88f}};
    const float beat = 0.6f, total = 32 * beat;
    int n = (int)(total * MRATE);
    music_buf = linearAlloc(n * sizeof(s16));
    if (!music_buf) return;
    for (int i = 0; i < n; i++) {
        float t = (float)i / MRATE, b = t / beat;
        int bar = ((int)(b / 4.f)) % 8, bib = ((int)b) % 4, step = ((int)(b * 2.f)) % 8;
        float bp = (b - 4.f * floorf(b / 4.f)) / 4.f;
        float e8 = b * 2.f - floorf(b * 2.f);
        int ci = bar_chord[bar];

        float pad = 0;
        for (int k = 0; k < 3; k++) pad += sinf(PI2 * chord[ci][k] * 0.5f * t);
        pad *= 0.07f * sinf(3.14159f * bp);

        float ph = bassf[ci] * t; ph -= floorf(ph);
        float bass = (4.f * fabsf(ph - 0.5f) - 1.f) * expf(-e8 * 5.f) * acc[step] * 0.35f;

        float hat = (step & 1) ? frand() * expf(-e8 * 30.f) * 0.08f : 0.f;

        float lt = (b - floorf(b)) * beat, lf = mel[bar][bib], lead = 0.f;
        if (lf > 0)
            lead = (sinf(PI2 * lf * lt) + 0.3f * sinf(PI2 * lf * 2.f * lt))
                 * expf(-lt * 3.5f) * (1.f - expf(-lt * 300.f)) * 0.18f;

        float x = pad + bass + hat + lead;
        float tail = total - t;
        if (tail < 0.03f) x *= tail / 0.03f;
        if (t < 0.005f) x *= t / 0.005f;
        if (x > 1.f) x = 1.f; else if (x < -1.f) x = -1.f;
        music_buf[i] = (s16)(x * 30000.f);
    }
    DSP_FlushDataCache(music_buf, n * sizeof(s16));
    music_len = n;
}

static void music_apply(void) {
    if (!audio_ok) return;
    float v = music_on ? (music_duck ? 0.12f : 0.35f) : 0.f;
    float mix[12] = {0};
    mix[0] = mix[1] = v;
    ndspChnSetMix(0, mix);
}

static void audio_init(void) {
    if (R_FAILED(ndspInit())) return;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    for (int ch = 0; ch < 8; ch++) {
        float mix[12] = {0};
        mix[0] = mix[1] = 0.8f;
        ndspChnReset(ch);
        ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
        ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
        ndspChnSetRate(ch, ch == 0 ? (float)MRATE : (float)SRATE);
        ndspChnSetMix(ch, mix);
    }
    audio_ok = 1;
    for (int i = 0; i < S_COUNT; i++) gen_sfx(i);
    gen_music();
    music_apply();
    if (music_buf) {
        memset(&wbs[0], 0, sizeof wbs[0]);
        wbs[0].data_vaddr = music_buf;
        wbs[0].nsamples = music_len;
        wbs[0].looping = true;
        ndspChnWaveBufAdd(0, &wbs[0]);
    }
}

static void sfx(int id) {
    if (!audio_ok || !sfx_buf[id]) return;
    int ch = sfx_next;
    if (++sfx_next > 7) sfx_next = 1;
    ndspChnWaveBufClear(ch);
    memset(&wbs[ch], 0, sizeof wbs[ch]);
    wbs[ch].data_vaddr = sfx_buf[id];
    wbs[ch].nsamples = sfx_len[id];
    wbs[ch].looping = false;
    ndspChnWaveBufAdd(ch, &wbs[ch]);
}

static void audio_exit(void) {
    if (!audio_ok) return;
    for (int ch = 0; ch < 8; ch++) ndspChnWaveBufClear(ch);
    ndspExit();
    for (int i = 0; i < S_COUNT; i++) if (sfx_buf[i]) linearFree(sfx_buf[i]);
    if (music_buf) linearFree(music_buf);
}

/* ------------------------------------------------------------ helpers */
static void money(char *out, int v) {
    if (v >= 1000000) sprintf(out, "$%d,%03d,%03d", v / 1000000, (v / 1000) % 1000, v % 1000);
    else if (v >= 1000) sprintf(out, "$%d,%03d", v / 1000, v % 1000);
    else sprintf(out, "$%d", v);
}

static void say(int next, const char *a, const char *b) {
    snprintf(L1, sizeof L1, "%s", a);
    snprintf(L2, sizeof L2, "%s", b);
    st = ST_MSG; nxt = next; timer = 0;
}

static void flash(const char *s) { snprintf(hint, sizeof hint, "%s", s); hint_t = 100; sfx(S_CLICK); }

static void counts(int *l, int *b) {
    *l = *b = 0;
    for (int i = pos; i < nshell; i++) { if (shells[i]) (*l)++; else (*b)++; }
}

static void reveal(int live) {
    if (live) { if (bel_live > 0) bel_live--; }
    else      { if (bel_blank > 0) bel_blank--; }
}

static int has(Actor *a, int it) {
    for (int i = 0; i < MAXI; i++) if (a->items[i] == it) return i;
    return -1;
}

static int can_use(int who, int it) {
    Actor *me = &A[who], *op = &A[1 - who];
    switch (it) {
        case I_SAW:  return !me->sawed;
        case I_CUFF: return !op->cuffed;
        case I_CIG:  return me->hp < me->max;
        case I_ADR:
            for (int i = 0; i < MAXI; i++)
                if (op->items[i] && op->items[i] != I_ADR && can_use(who, op->items[i])) return 1;
            return 0;
        default: return 1;
    }
}

static void give(Actor *a) {
    int free_slots[MAXI], n = 0;
    for (int i = 0; i < MAXI; i++) if (!a->items[i]) free_slots[n++] = i;
    if (n) a->items[free_slots[rand() % n]] = 1 + rand() % (I_COUNT - 1);
}

/* ------------------------------------------------------------ game logic */
static void use_item(int who, int it, int stolen) {
    Actor *me = &A[who], *op = &A[1 - who];
    char a[96], b[96] = "";
    const char *nm = who ? "Dealer" : "You";
    if (stolen) snprintf(a, sizeof a, "ADRENALINE! %s steal%s %s.", nm, who ? "s" : "", INAMEL[it]);
    else        snprintf(a, sizeof a, "%s %s %s.", nm, who ? "uses" : "use", INAMEL[it]);

    {
        static const int sm[] = {S_CLICK, S_GLASS, S_BEER, S_HEAL, S_SAW, S_CUFF, S_CLICK, S_PHONE, S_INV, S_ITEM};
        if (stolen) sfx(S_ADR);
        sfx(sm[it]);
    }
    switch (it) {
    case I_GLASS:
        known[who][pos] = shells[pos];
        if (who == 0) snprintf(b, sizeof b, "The shell is %s.", shells[pos] ? "LIVE" : "BLANK");
        else snprintf(b, sizeof b, "He studies the chamber closely.");
        break;
    case I_BEER:
        snprintf(b, sizeof b, "Ejected a %s shell.", shells[pos] ? "LIVE" : "BLANK");
        reveal(shells[pos]); pos++;
        break;
    case I_CIG:
        if (me->hp < me->max) me->hp++;
        snprintf(b, sizeof b, "+1 charge.");
        break;
    case I_SAW:
        me->sawed = 1;
        snprintf(b, sizeof b, "Next live shell does double damage!");
        break;
    case I_CUFF:
        op->cuffed = 1;
        snprintf(b, sizeof b, "%s will skip the next turn.", who ? "You" : "Dealer");
        break;
    case I_MED:
        if (rand() % 2) { me->hp += 2; sfx(S_HEAL); if (me->hp > me->max) me->hp = me->max; snprintf(b, sizeof b, "Tastes fine. +2 charges."); }
        else { me->hp--; sfx(S_BAD); snprintf(b, sizeof b, "Ugh... -1 charge."); if (who == 0) flash_t = 20; else { dmood = 1; dmood_t = 40; } }
        break;
    case I_PHONE: {
        int rem = nshell - pos;
        if (rem < 2) snprintf(b, sizeof b, "...No signal. Nothing learned.");
        else {
            int j = pos + 1 + rand() % (rem - 1);
            known[who][j] = shells[j];
            if (who == 0) snprintf(b, sizeof b, "Shell #%d from now is %s.", j - pos + 1, shells[j] ? "LIVE" : "BLANK");
            else snprintf(b, sizeof b, "He listens to the phone.");
        }
        break; }
    case I_INV:
        shells[pos] ^= 1;
        known[who][pos] = (known[who][pos] < 0) ? -1 : shells[pos];
        known[1 - who][pos] = -1;
        snprintf(b, sizeof b, "The current shell was flipped.");
        break;
    }
    say(ST_ADV, a, b);
}

static void steal(int who, int slot) {
    Actor *op = &A[1 - who];
    int it = op->items[slot];
    op->items[slot] = 0;
    use_item(who, it, 1);
}

static void load_shotgun(void) {
    nshell = 2 + rand() % 7;
    int live = 1 + rand() % (nshell - 1);
    for (int i = 0; i < nshell; i++) shells[i] = (i < live);
    for (int i = nshell - 1; i > 0; i--) { int j = rand() % (i + 1); int t = shells[i]; shells[i] = shells[j]; shells[j] = t; }
    sfx(S_LOAD);
    pos = 0; nlive0 = live; nblank0 = nshell - live; bel_live = live; bel_blank = nblank0;
    for (int p = 0; p < 2; p++) for (int i = 0; i < MAXS; i++) known[p][i] = -1;
    if (round_ >= 2) {
        int k = 1 + rand() % 4;
        for (int w = 0; w < 2; w++) for (int j = 0; j < k; j++) give(&A[w]);
    }
    char a[96];
    snprintf(a, sizeof a, "Loading %d shells: %d LIVE, %d BLANK", nshell, nlive0, nblank0);
    say(ST_ADV, a, round_ >= 2 ? "Fresh items for both sides." : "Shuffled. Round 1 has no items.");
}

static void start_round(void) {
    int hp = 2 + rand() % 3;
    for (int w = 0; w < 2; w++) {
        A[w].hp = A[w].max = hp; A[w].cuffed = A[w].sawed = 0;
        memset(A[w].items, 0, sizeof A[w].items);
    }
    turn = 0; dmood = 0;
    char a[96], b[96];
    snprintf(a, sizeof a, "ROUND %d of 3", round_);
    snprintf(b, sizeof b, "Each side has %d charges.", hp);
    say(ST_LOAD, a, b);
}

static void advance(void) {
    char a[96], b[96], m[24];
    if (P.hp <= 0) {
        if (run_ > 1 && pot > 0) { money(m, pot); snprintf(b, sizeof b, "Double or nothing lost: -%s", m); }
        else snprintf(b, sizeof b, "GAME OVER");
        pot = 0;
        sfx(S_LOSE); music_duck = 1; music_apply();
        say(ST_GAMEOVER, "Your ears ring. Everything goes dark.", b);
        return;
    }
    if (D.hp <= 0) {
        dmood = 2; sfx(S_WIN);
        if (round_ >= 3) {
            pot = (run_ == 1) ? 70000 : pot * 2;
            if (pot > 900000000) pot = 900000000;
            money(m, pot);
            snprintf(a, sizeof a, "Run %d cleared!", run_);
            snprintf(b, sizeof b, "You are holding %s.", m);
            say(ST_DON, a, b);
        } else say(ST_NEWROUND, "The dealer is down!", "On to the next round...");
        return;
    }
    if (pos >= nshell) { load_shotgun(); return; }
    if (turn == 0) { st = ST_PLAYER; cur = 0; }
    else { st = ST_DEALER; timer = 50; }
}

static void shoot(int shooter, int target) {
    Actor *s = &A[shooter], *t = &A[target];
    int live = shells[pos];
    reveal(live); pos++;
    sfx(live ? S_BLAM : S_BLANK);
    int dmg = live ? (s->sawed ? 2 : 1) : 0;
    s->sawed = 0;
    char a[96], b[96];
    if (shooter == 0) snprintf(a, sizeof a, target == 0 ? "You press the barrel to your chin..." : "You aim at the dealer...");
    else snprintf(a, sizeof a, target == 1 ? "Dealer aims at himself..." : "Dealer aims at you...");
    if (live) {
        t->hp -= dmg; if (t->hp < 0) t->hp = 0;
        if (target) { dmood = 1; dmood_t = 40; } else flash_t = 24;
        snprintf(b, sizeof b, "BLAM! LIVE. -%d charge%s.", dmg, dmg > 1 ? "s" : "");
    } else snprintf(b, sizeof b, "click... BLANK.");

    if (target == shooter && !live) {
        strncat(b, " Go again.", sizeof b - strlen(b) - 1);
    } else {
        int o = 1 - shooter;
        if (A[o].cuffed) {
            A[o].cuffed = 0;
            strncat(b, " (cuffed: again)", sizeof b - strlen(b) - 1);
            turn = shooter;
        } else turn = o;
    }
    say(ST_ADV, a, b);
}

/* ------------------------------------------------------------ dealer AI */
#define TRY(it) do { int _i = has(&D, it); if (_i >= 0 && can_use(1, it)) { D.items[_i] = 0; use_item(1, it, 0); return; } } while (0)

static void dealer_act(void) {
    int rem = nshell - pos;
    int eff = known[1][pos];
    if (eff < 0) {
        if (bel_live <= 0) eff = 0; else if (bel_blank <= 0) eff = 1;
        if (eff >= 0) known[1][pos] = eff;
    }
    TRY(I_CIG);
    if (D.hp >= 2 && D.hp < D.max) TRY(I_MED);
    if (eff < 0) TRY(I_GLASS);
    if (eff < 0 && rem >= 3) TRY(I_PHONE);
    {
        int a = has(&D, I_ADR);
        if (a >= 0 && can_use(1, I_ADR)) {
            int start = rand() % MAXI;
            for (int k = 0; k < MAXI; k++) {
                int sl = (start + k) % MAXI, it = P.items[sl];
                if (it && it != I_ADR && can_use(1, it)) { D.items[a] = 0; steal(1, sl); return; }
            }
        }
    }
    if (eff == 0) {
        TRY(I_INV);               /* flip a blank into a live one */
        shoot(1, 1);              /* blank: shoot self, keep turn */
        return;
    }
    if (eff == 1) {
        TRY(I_CUFF);
        TRY(I_SAW);
        shoot(1, 0);
        return;
    }
    /* unknown shell */
    TRY(I_CUFF);
    if (rem > 1 && has(&D, I_BEER) >= 0 && rand() % 2) TRY(I_BEER);
    float p = (bel_live + bel_blank) > 0 ? (float)bel_live / (bel_live + bel_blank) : 0.5f;
    if (p >= 0.6f && P.hp <= 2) TRY(I_SAW);
    if (p > 0.5f || (D.hp <= 1 && p > 0.0f)) shoot(1, 0);
    else shoot(1, 1);
}

/* ------------------------------------------------------------ player actions */
static void player_use_slot(int i) {
    int it = P.items[i];
    if (!it) return;
    if (!can_use(0, it)) {
        if (it == I_CIG) flash("You're already at full charges.");
        else if (it == I_SAW) flash("The gun is already sawed off.");
        else if (it == I_CUFF) flash("Dealer is already cuffed.");
        else if (it == I_ADR) flash("Nothing usable to steal.");
        return;
    }
    if (it == I_ADR) { adr_slot = i; cur = 0; st = ST_ADR_PICK; return; }
    P.items[i] = 0;
    use_item(0, it, 0);
}

static void new_game(void) {
    run_ = 1; round_ = 1; pot = 0;
    music_duck = 0; music_apply();
    start_round();
}

/* ------------------------------------------------------------ drawing */
static void txt(float x, float y, float s, u32 c, int center, const char *str) {
    C2D_Text t;
    C2D_TextParse(&t, tbuf, str);
    C2D_TextOptimize(&t);
    if (center) { float w, h; C2D_TextGetDimensions(&t, s, s, &w, &h); x -= w / 2; }
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, s, s, c);
}

static void rect(float x, float y, float w, float h, u32 c) { C2D_DrawRectSolid(x, y, 0.4f, w, h, c); }

static void box(float x, float y, float w, float h, u32 fill, u32 border) {
    rect(x - 2, y - 2, w + 4, h + 4, border);
    rect(x, y, w, h, fill);
}

static void button(float x, float y, float w, float h, const char *label, u32 col, int sel) {
    box(x, y, w, h, col, sel ? cWhite : cBlack);
    txt(x + w / 2, y + h / 2 - 9, 0.6f, cWhite, 1, label);
}

static void charges(float x, float y, int hp, int max) {
    for (int i = 0; i < max; i++) rect(x + i * 12, y, 9, 15, i < hp ? cYellow : cDark);
}

/* The dealer's face (a hand-drawn blob with two dots and a "u" mouth). */
static void draw_face(float cx, float cy, float k, int mood) {
    float w = 700 * k, h = 570 * k, t = 45 * k;
    C2D_DrawEllipseSolid(cx - w / 2, cy - h / 2, 0.1f, w, h, cBlack);
    C2D_DrawEllipseSolid(cx - w / 2 + t, cy - h / 2 + t, 0.11f, w - 2 * t, h - 2 * t, cWhite);
    float ey = cy - 8 * k;
    if (mood == 2) {
        for (int s = -1; s <= 1; s += 2) {
            float ex = cx + s * 190 * k, r = 22 * k;
            C2D_DrawLine(ex - r, ey - r, cBlack, ex + r, ey + r, cBlack, 8 * k, 0.2f);
            C2D_DrawLine(ex - r, ey + r, cBlack, ex + r, ey - r, cBlack, 8 * k, 0.2f);
        }
    } else {
        float r = (mood == 1 ? 32 : 24) * k;
        C2D_DrawCircleSolid(cx - 190 * k, ey, 0.2f, r, cBlack);
        C2D_DrawCircleSolid(cx + 190 * k, ey, 0.2f, r, cBlack);
    }
    static const float mx[] = {-80, -88, -58, -5, 40, 66, 66};
    static const float my[] = {-12,  35,  85, 100, 95, 45, -12};
    if (mood == 0) {
        for (int i = 0; i < 7; i++) {
            float x0 = cx + mx[i] * k, y0 = cy + my[i] * k;
            C2D_DrawCircleSolid(x0, y0, 0.2f, t / 2, cBlack);
            if (i < 6) C2D_DrawLine(x0, y0, cBlack, cx + mx[i + 1] * k, cy + my[i + 1] * k, cBlack, t, 0.2f);
        }
    } else if (mood == 1) {
        C2D_DrawCircleSolid(cx, cy + 70 * k, 0.2f, 52 * k, cBlack);
        C2D_DrawCircleSolid(cx, cy + 70 * k, 0.21f, 26 * k, cWhite);
    } else {
        C2D_DrawLine(cx - 70 * k, cy + 70 * k, cBlack, cx + 70 * k, cy + 70 * k, cBlack, t, 0.2f);
    }
}

static void slot_xywh(int i, float *x, float *y) { *x = 10 + (i % 4) * 77; *y = 84 + (i / 4) * 58; }

static void draw_slots(Actor *a, int selected, int dim) {
    for (int i = 0; i < MAXI; i++) {
        float x, y; slot_xywh(i, &x, &y);
        int it = a->items[i];
        u32 col = it ? ICOL[it] : cDark;
        if (dim && it) col = C2D_Color32(70, 70, 80, 255);
        box(x, y, 72, 52, col, (selected == i) ? cWhite : cBlack);
        if (it) txt(x + 36, y + 17, 0.5f, cWhite, 1, INAME[it]);
    }
}

static void draw_top(void) {
    char buf[96], m[24];
    rect(0, 0, 400, 240, cBG);
    if (st == ST_TITLE) {
        draw_face(200, 95, 0.32f, 0);
        txt(200, 4, 0.9f, cWhite, 1, "JIMBO'S ROULETTE");
        txt(200, 195, 0.55f, cWhite, 1, "A fan-made shotgun-roulette tribute");
        txt(200, 215, 0.45f, C2D_Color32(170, 170, 190, 255), 1, "START: quit     SELECT: music on/off");
        return;
    }
    snprintf(buf, sizeof buf, "ROUND %d/3    RUN %d", round_, run_);
    txt(200, 3, 0.5f, cWhite, 1, buf);
    if (run_ > 1) { money(m, pot); snprintf(buf, sizeof buf, "At stake: %s", m); txt(200, 17, 0.4f, cYellow, 1, buf); }

    draw_face(200, 98, 0.24f, dmood);

    /* dealer panel */
    txt(8, 22, 0.45f, cWhite, 0, "DEALER");
    charges(8, 36, D.hp, D.max);
    for (int i = 0; i < MAXI; i++) {
        float x = 6 + (i % 2) * 52, y = 58 + (i / 2) * 29;
        if (D.items[i]) { box(x, y, 48, 24, ICOL[D.items[i]], cBlack); txt(x + 24, y + 6, 0.38f, cWhite, 1, INAME[D.items[i]]); }
        else rect(x, y, 48, 24, cPanel);
    }

    /* player panel + chamber info */
    txt(292, 22, 0.45f, cWhite, 0, "YOU");
    charges(292, 36, P.hp, P.max);
    for (int i = pos; i < nshell && nshell > 0; i++) {
        float x = 292 + (i - pos) * 13;
        int k = known[0][i];
        rect(x, 60, 11, 16, k < 0 ? cGray : (k ? cRed : cBlue));
        if (k < 0) txt(x + 5, 62, 0.35f, cBlack, 1, "?");
    }
    if (nshell > 0) {
        snprintf(buf, sizeof buf, "Load: %dL / %dB", nlive0, nblank0);
        txt(292, 82, 0.4f, cWhite, 0, buf);
        snprintf(buf, sizeof buf, "Left: %dL / %dB", bel_live, bel_blank);
        txt(292, 96, 0.4f, cWhite, 0, buf);
    }
    int sawed = P.sawed || D.sawed;
    rect(292, 124, 22, 8, cBrown);
    rect(314, 125, sawed ? 28 : 78, 5, cGray);
    if (sawed) rect(342, 125, 4, 5, cRed);
    if (P.cuffed) txt(292, 140, 0.4f, cYellow, 0, "You: CUFFED");
    if (D.cuffed) txt(292, 152, 0.4f, cYellow, 0, "Dealer: CUFFED");
    if (P.sawed)  txt(292, 164, 0.4f, cRed, 0, "Gun: SAWED (you)");
    if (D.sawed)  txt(292, 164, 0.4f, cRed, 0, "Gun: SAWED (dealer)");

    /* message box */
    box(10, 178, 380, 54, cPanel, cWhite);
    if (st == ST_PLAYER || st == ST_ADR_PICK) {
        txt(20, 186, 0.55f, cWhite, 0, "Your turn.");
        txt(20, 208, 0.5f, C2D_Color32(190, 190, 210, 255), 0, "Shoot the dealer or yourself, or use an item.");
    } else if (st == ST_DEALER) {
        txt(20, 186, 0.55f, cWhite, 0, "The dealer is thinking...");
    } else {
        txt(20, 186, 0.5f, cWhite, 0, L1);
        txt(20, 208, 0.5f, cYellow, 0, L2);
    }
    if (flash_t > 0) rect(0, 0, 400, 240, C2D_Color32(255, 0, 0, 40 + flash_t * 5));
}

static void draw_bottom(void) {
    char buf[96], m[24];
    rect(0, 0, 320, 240, cBG);
    switch (st) {
    case ST_TITLE:
        txt(160, 20, 0.7f, cWhite, 1, "Press A or tap to play");
        txt(160, 60, 0.45f, cWhite, 1, "Shoot the dealer. Don't get shot.");
        txt(160, 80, 0.45f, cWhite, 1, "Shooting yourself with a BLANK = go again.");
        txt(160, 100, 0.45f, cWhite, 1, "Rounds 2-3 hand out items.");
        txt(160, 120, 0.45f, cWhite, 1, "Clear 3 rounds to win $70,000,");
        txt(160, 140, 0.45f, cWhite, 1, "then cash out or go DOUBLE OR NOTHING.");
        txt(160, 190, 0.4f, C2D_Color32(150, 150, 170, 255), 1, "D-Pad + A/B or touch screen");
        break;
    case ST_PLAYER:
        button(10, 12, 150, 58, "SHOOT DEALER", C2D_Color32(170, 40, 40, 255), cur == 0);
        button(165, 12, 145, 58, "SHOOT SELF", C2D_Color32(50, 80, 170, 255), cur == 1);
        draw_slots(&P, cur >= 2 ? cur - 2 : -1, 0);
        if (hint_t > 0) txt(160, 208, 0.5f, cYellow, 1, hint);
        else if (cur == 0) txt(160, 208, 0.5f, cWhite, 1, "Fire at the dealer. Turn passes.");
        else if (cur == 1) txt(160, 208, 0.5f, cWhite, 1, "Fire at yourself. BLANK = go again.");
        else if (P.items[cur - 2]) txt(160, 208, 0.5f, cWhite, 1, IDESC[P.items[cur - 2]]);
        break;
    case ST_ADR_PICK:
        txt(160, 14, 0.6f, cYellow, 1, "ADRENALINE: steal which item?");
        txt(160, 40, 0.45f, cWhite, 1, "A / tap = steal    B = cancel");
        draw_slots(&D, cur, 0);
        if (hint_t > 0) txt(160, 208, 0.5f, cYellow, 1, hint);
        else if (D.items[cur]) txt(160, 208, 0.5f, cWhite, 1, IDESC[D.items[cur]]);
        break;
    case ST_DON:
        money(m, pot);
        snprintf(buf, sizeof buf, "CASH OUT  %s", m);
        button(20, 30, 280, 70, buf, C2D_Color32(40, 130, 60, 255), cur == 0);
        money(m, pot * 2);
        snprintf(buf, sizeof buf, "DOUBLE OR NOTHING  %s", m);
        button(20, 125, 280, 70, buf, C2D_Color32(170, 40, 40, 255), cur == 1);
        txt(160, 210, 0.42f, cWhite, 1, "Lose a single round and you lose it ALL.");
        break;
    case ST_MSG:
        draw_slots(&P, -1, 1);
        txt(160, 205, 0.5f, cWhite, 1, "A / tap to continue");
        break;
    case ST_GAMEOVER:
    case ST_WIN:
        txt(160, 100, 0.7f, cWhite, 1, "Press A or tap");
        txt(160, 125, 0.7f, cWhite, 1, "to return to title");
        break;
    default:
        draw_slots(&P, -1, 1);
        break;
    }
}

/* ------------------------------------------------------------ update */
static int hit(touchPosition t, float x, float y, float w, float h) {
    return t.px >= x && t.px < x + w && t.py >= y && t.py < y + h;
}

static void update(u32 kDown, touchPosition tp) {
    int touched = (kDown & KEY_TOUCH) != 0;
    int c0 = cur;
    if (kDown & KEY_SELECT) { music_on = !music_on; music_apply(); }
    int confirm = (kDown & KEY_A) || touched;
    if (hint_t > 0) hint_t--;
    if (flash_t > 0) flash_t--;
    if (dmood == 1 && dmood_t > 0 && --dmood_t == 0) dmood = 0;

    switch (st) {
    case ST_TITLE: if (confirm) new_game(); break;
    case ST_ADV: advance(); break;
    case ST_LOAD: load_shotgun(); break;
    case ST_NEWROUND: round_++; start_round(); break;
    case ST_MSG:
        timer++;
        if (confirm || (kDown & KEY_B) || timer > 150) st = nxt;
        break;
    case ST_DEALER:
        if (--timer <= 0) dealer_act();
        break;
    case ST_PLAYER:
        if (kDown & KEY_LEFT)  { if (cur < 2) cur ^= 1; else if ((cur - 2) % 4 > 0) cur--; }
        if (kDown & KEY_RIGHT) { if (cur < 2) cur ^= 1; else if ((cur - 2) % 4 < 3) cur++; }
        if (kDown & KEY_UP)    { if (cur >= 6) cur -= 4; else if (cur >= 2) cur = ((cur - 2) >= 2) ? 1 : 0; }
        if (kDown & KEY_DOWN)  { if (cur < 2) cur = 2 + cur * 2; else if (cur < 6) cur += 4; }
        if (kDown & KEY_A) {
            if (cur == 0) shoot(0, 1);
            else if (cur == 1) shoot(0, 0);
            else player_use_slot(cur - 2);
        } else if (touched) {
            if (hit(tp, 10, 12, 150, 58)) shoot(0, 1);
            else if (hit(tp, 165, 12, 145, 58)) shoot(0, 0);
            else for (int i = 0; i < MAXI; i++) {
                float x, y; slot_xywh(i, &x, &y);
                if (hit(tp, x, y, 72, 52)) { cur = 2 + i; player_use_slot(i); break; }
            }
        }
        break;
    case ST_ADR_PICK: {
        if (kDown & KEY_LEFT)  { if (cur % 4 > 0) cur--; }
        if (kDown & KEY_RIGHT) { if (cur % 4 < 3) cur++; }
        if (kDown & KEY_UP)    { if (cur >= 4) cur -= 4; }
        if (kDown & KEY_DOWN)  { if (cur < 4) cur += 4; }
        if (kDown & KEY_B) { st = ST_PLAYER; cur = 2 + adr_slot; break; }
        int pick = -1;
        if (kDown & KEY_A) pick = cur;
        else if (touched) {
            for (int i = 0; i < MAXI; i++) {
                float x, y; slot_xywh(i, &x, &y);
                if (hit(tp, x, y, 72, 52)) { cur = i; pick = i; break; }
            }
        }
        if (pick >= 0) {
            int it = D.items[pick];
            if (!it) flash("Empty slot.");
            else if (it == I_ADR) flash("Can't steal adrenaline.");
            else if (!can_use(0, it)) flash("You can't use that right now.");
            else { P.items[adr_slot] = 0; steal(0, pick); }
        }
        break; }
    case ST_DON: {
        if (kDown & (KEY_UP | KEY_DOWN)) cur ^= 1;
        int pick = -1;
        if (kDown & KEY_A) pick = cur;
        else if (touched) {
            if (hit(tp, 20, 30, 280, 70)) pick = 0;
            else if (hit(tp, 20, 125, 280, 70)) pick = 1;
        }
        if (pick == 0) {
            char m[24], b[96]; money(m, pot);
            snprintf(b, sizeof b, "You cash out with %s!", m);
            sfx(S_WIN);
            say(ST_WIN, b, "The dealer just smiles.");
        } else if (pick == 1) { run_++; round_ = 1; start_round(); }
        break; }
    case ST_GAMEOVER:
    case ST_WIN:
        if (confirm) { st = ST_TITLE; music_duck = 0; music_apply(); }
        break;
    }
    if (cur != c0 && (kDown & (KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN))) sfx(S_SELECT);
}

/* ------------------------------------------------------------ main */
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    tbuf = C2D_TextBufNew(4096);

    cBlack = C2D_Color32(0, 0, 0, 255);       cWhite = C2D_Color32(255, 255, 255, 255);
    cRed = C2D_Color32(210, 50, 50, 255);     cBlue = C2D_Color32(60, 100, 220, 255);
    cGray = C2D_Color32(150, 150, 160, 255);  cDark = C2D_Color32(45, 40, 55, 255);
    cYellow = C2D_Color32(250, 210, 60, 255); cBrown = C2D_Color32(120, 80, 40, 255);
    cBG = C2D_Color32(24, 18, 30, 255);       cPanel = C2D_Color32(40, 34, 52, 255);
    ICOL[I_GLASS] = C2D_Color32(40, 120, 170, 255); ICOL[I_BEER]  = C2D_Color32(180, 120, 20, 255);
    ICOL[I_CIG]   = C2D_Color32(170, 70, 70, 255);  ICOL[I_SAW]   = C2D_Color32(110, 110, 125, 255);
    ICOL[I_CUFF]  = C2D_Color32(80, 80, 190, 255);  ICOL[I_MED]   = C2D_Color32(60, 140, 70, 255);
    ICOL[I_PHONE] = C2D_Color32(140, 70, 160, 255); ICOL[I_INV]   = C2D_Color32(200, 100, 40, 255);
    ICOL[I_ADR]   = C2D_Color32(200, 40, 100, 255);

    /* loading screen while the sounds are synthesized */
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(tbuf);
    C2D_TargetClear(top, cBG);
    C2D_SceneBegin(top);
    txt(200, 105, 0.7f, cWhite, 1, "Loading sounds...");
    C2D_TargetClear(bot, cBG);
    C2D_SceneBegin(bot);
    C3D_FrameEnd(0);
    audio_init();

    srand((unsigned)time(NULL));
    st = ST_TITLE; dmood = 0; round_ = 1; run_ = 1; nshell = 0;
    memset(&A, 0, sizeof A);

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) break;
        touchPosition tp = {0, 0};
        hidTouchRead(&tp);

        update(kDown, tp);
        frame++;

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TextBufClear(tbuf);
        C2D_TargetClear(top, cBG);
        C2D_SceneBegin(top);
        draw_top();
        C2D_TargetClear(bot, cBG);
        C2D_SceneBegin(bot);
        draw_bottom();
        C3D_FrameEnd(0);
    }

    audio_exit();
    C2D_TextBufDelete(tbuf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
