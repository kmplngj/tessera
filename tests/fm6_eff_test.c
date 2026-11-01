/* SPDX-License-Identifier: GPL-3.0-only */
/* Regression for Felucca #419 (8f7d07f): latent FM6 patch updates must not
 * resurrect an effective patch cached before the track changed engines.
 * Use Tessera's preset requests and audio-side switch, including fade-out. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(const char *what, int ok)
{
    printf("FM6 effective patch: %s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static void blocks(track_t *t, uint32_t n)
{
    int32_t out[CTL];
    while (n--) { engine_block(t); track_render(t, out, CTL); }
}

/* A first note from reset engine/voice state, preserving the effective cache. */
static uint64_t first_note(track_t *t)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    int32_t out[CTL];
    memset(t->v, 0, sizeof t->v);
    t->nmono = t->mono_note = t->xp_n = 0;
    eng_state_clear((uint32_t)(t - trk));
    trk_note_on(t, 60, 100);
    for (uint32_t b = 0; b < 16; b++) {
        track_render(t, out, CTL);
        for (uint32_t i = 0; i < CTL; i++)
            hash = (hash ^ (uint32_t)out[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

int main(void)
{
    uint8_t patch[FP_SIZE + 1u], expected[FP_SIZE + 1u];
    for (uint32_t alias = 0; alias < 2; alias++)
      for (uint32_t macro = 0; macro < 2; macro++)
        for (uint32_t tr = 0; tr < NPART; tr++) {
            track_t *t = &trk[tr];
            uint64_t held, cold;
            int content;
            memset(trk, 0, sizeof trk);
            eng_state_reset();
            memset(fm6_eff, 0, sizeof fm6_eff);
            fm6_fn_reset(); host_tracks_init();
            host_preset(t, ENGI_FM6, 1);
            for (uint32_t k = 0; k < 8; k++) t->p[P_E0 + k] = 0;
            t->p[P_E1] = macro ? 2 : 0;
            blocks(t, 1);
            trk_note_on(t, 64, 100);
            blocks(t, 1);
            host_preset_req(t, HOST_SYNTH, HOST_PLAIN);
            blocks(t, XF_BLOCKS + 2u);
            check("switch leaves FM6 after its release fade", t->engine == HOST_SYNTH);
            check("leaving FM6 invalidates the audio-owned effective patch", !fm6_eff[tr].ok);
            fm6_unpack(FM6_INIT, patch);
            for (uint32_t i = 0; i < 255; i++) {
                patch[FP_NAME] = (uint8_t)('A' + i % 26);
                fm6_put_patch(tr, patch, i & 1u);
                blocks(t, 1);
            }
            /* Exercise cache invalidation independently of widening: the next
             * preset publication deliberately aliases the cached generation.
             * Without fm6_eff_drop this case keeps the old patch forever. */
            if (alias) fm6_pgen[tr] = fm6_eff[tr].gen - 1u;
            /* The ordinary switch back publishes one more factory patch. */
            host_preset_req(t, ENGI_FM6, 0);
            check(alias ? "forced generation alias isolates cache invalidation" :
                          "256 publications do not wrap the patch generation",
                  alias ? fm6_pgen[tr] == fm6_eff[tr].gen :
                          fm6_pgen[tr] == fm6_eff[tr].gen + 256u);
            for (uint32_t k = 0; k < 8; k++) t->p[P_E0 + k] = 0;
            t->p[P_E1] = macro ? 2 : 0;
            blocks(t, XF_BLOCKS + 2u);
            memcpy(expected, fm6_patch[tr], FP_SIZE);
            expected[FP_FB] = (uint8_t)clamp(expected[FP_FB] + t->p[P_E1], 0, 7);
            content = !memcmp(fm6_eff[tr].p, expected, FP_SIZE);
            held = first_note(t);
            fm6_eff[tr].ok = 0;
            cold = first_note(t);
            check("256 updates across engines use the published patch and macros", content);
            check("first note equals a note with a freshly built effective patch", held == cold);
        }
    return bad != 0;
}
