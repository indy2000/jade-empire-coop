/* combat.inc -- 0.10.0: first combat sync. The host owns every enemy; the guest mirrors them.
 *
 * Facts it relies on (owner's Dawn Star save, NOTES "Combat"):
 *  - fighters = participants of the player's combat areas: GetCurrentCombatArea(player, k, 0) (#939),
 *    GetCombatAreaParticipantByIndex(area, i) (#881); enemies have GetStandardFaction (#713) 1, 3 or 6;
 *  - SetDisableCreatureCombatAI(o, TRUE) (#935) after the fight started freezes an enemy (the global
 *    #930 before the fight did not); SetHenchmanFollowPlayerInCombat(id, TRUE) (#1045) keeps the
 *    companion out of the fight; GetHealth #49, SetHealth #156, GetIsDead #140, EffectDeath #927;
 *  - the same creature has the same tag hash in both games; it is matched by tag + where it stood.
 *
 * Host: lists its enemies (slots 220+j), reads one per frame (health, dead, position) and sends a
 *       snapshot 'JEF1' every 250 ms on channel 0. A guest damage request 'JEDM' (channel 1, reliable)
 *       lowers that enemy's health (EffectDeath at 0).
 * Guest: on its own fight start keeps its companion out and freezes its enemies (slots 240+j); matches
 *        them to the host's; per frame for one matched enemy: dead on the host -> kill; its health below
 *        what we set -> the guest hit it: send the difference to the host; otherwise take the host's
 *        health; far from the host's position -> walk/run there (jump when > 10 m).
 * Not yet: enemies attacking the guest's puppet on the host, attack animations on the guest. */

#define CB_MAX 16
#define CB_HSLOT 220      /* host enemies */
#define CB_GSLOT 240      /* guest enemies */
#define CB_TMP   219
typedef struct { int used; int32_t tag; int16_t sx, sy; int hp, dead; int32_t x, y, z; } CbHost;
typedef struct { int used; int32_t tag; int16_t sx, sy; int hp, dead; int32_t x, y, z; int match, lastSet; uint32_t pend, moveTick; } CbLocal;
typedef struct { int valid; int32_t tag; int16_t sx, sy; int hp, dead; int32_t x, y, z; } CbRemote;
static CbHost g_cbH[CB_MAX]; static CbLocal g_cbL[CB_MAX]; static CbRemote g_cbR[CB_MAX];
static int g_cbInFight, g_cbEnumC, g_cbRead, g_cbNH, g_cbNL, g_cbHenchDone;
static uint32_t g_cbSnapTick, g_cbEnumTick, g_cbSnaps, g_cbSnapsIn, g_cbDmgSent, g_cbDmgApplied, g_cbKills, g_cbSets, g_cbMoves, g_cbFreezes;

static void n_push_slot(Ncs *s, int slot) { n_consti(s, slot); n_action(s, R_GETPLAYER, 0); n_action(s, R_GETLOCALOBJECT, 2); }
static int cb_run(const char *name, Ncs *s) { n_retn(s); n_finish(s); if (ms_store(name, s->b, s->n)) return 0; return vm_run(name, 0x7F000000) == 1; }
static int cb_eval(Ncs *s, int extra, int *out) { n_int_main_end(s, extra); if (ms_store("jbc_ev", s->b, s->n)) return 0; return vm_eval("jbc_ev", out); }
static int cb_slot_int(int slot, uint16_t routine, int *out) { Ncs s; n_int_main_begin(&s); n_push_slot(&s, slot); n_action(&s, routine, 1); return cb_eval(&s, 4, out); }
static int cb_slot_pos(int slot, int i, int *out) { Ncs s; n_int_main_begin(&s); n_push_slot(&s, slot); n_action(&s, R_GETPOSITION, 1);
    n_cptopsp(&s, -12 + 4 * i, 4); n_constf(&s, 1000.0f); n_mulff(&s); n_action(&s, R_FLOATTOINT, 1); return cb_eval(&s, 16, out); }
static int cb_in_fight(void) { Ncs s; n_int_main_begin(&s); n_action(&s, 320 /* GetIsInCombat */, 0); int v = 0; return cb_eval(&s, 4, &v) && v; }
/* tag hash of the object in a slot (same NCS as jbn_area) */
static int cb_tag(int slot, int32_t *out) {
    static const char *ALPHA = "abcdefghijklmnopqrstuvwxyz0123456789_";
    Ncs s; n_int_main_begin(&s); n_consti(&s, 0); n_push_slot(&s, slot); n_action(&s, 168, 1); n_action(&s, 61, 1);
    for (int i = 0; i < 16; i++) {
        n_consti(&s, 1); n_consti(&s, i); n_cptopsp(&s, -12, 4); n_action(&s, 65, 3);
        n_consts(&s, ALPHA); n_action(&s, 66, 2); n_consti(&s, 2); nb(&s, 0x14); nb(&s, 0x20);
        n_cptopsp(&s, -12, 4); n_consti(&s, 31); nb(&s, 0x16); nb(&s, 0x20); nb(&s, 0x14); nb(&s, 0x20);
        n_cpdownsp(&s, -12, 4); n_movsp(&s, -4);
    }
    n_cptopsp(&s, -8, 4); int v = 0; if (!cb_eval(&s, 12, &v)) return 0; *out = v; return 1;
}
/* slot CB_TMP = participant i of combat area k. Two scripts: area -> slot CB_AREA, then the participant.
   Doing both in one script returned an invalid object (owner's save, copy B), unknown why. */
#define CB_AREA 218
static void cb_pick(int k, int i) {
    if (i == 0) { Ncs a; n_void_main_begin(&a); n_consti(&a, 0); n_consti(&a, k); n_action(&a, R_GETPLAYER, 0); n_action(&a, 939, 3);
                  n_consti(&a, CB_AREA); n_action(&a, R_GETPLAYER, 0); n_action(&a, R_SETLOCALOBJECT, 3); cb_run("jbc_area", &a); }
    Ncs s; n_void_main_begin(&s); n_consti(&s, i); n_push_slot(&s, CB_AREA); n_action(&s, 881, 2);
    n_consti(&s, CB_TMP); n_action(&s, R_GETPLAYER, 0); n_action(&s, R_SETLOCALOBJECT, 3); cb_run("jbc_pick", &s); }
static int cb_valid(int slot) { Ncs s; n_int_main_begin(&s); n_push_slot(&s, slot); n_action(&s, R_GETISOBJECTVALID, 1); int v = 0; return cb_eval(&s, 4, &v) && v; }
/* index+1 of the table slot (base..base+15) holding the same object as CB_TMP, 0 if none (EQUALOO 0x0B 0x22) */
static int cb_known(int base, int guest) {
    Ncs s; n_int_main_begin(&s); n_consti(&s, 0);
    for (int j = 0; j < CB_MAX; j++) { if (!(guest ? g_cbL[j].used : g_cbH[j].used)) continue; n_push_slot(&s, CB_TMP); n_push_slot(&s, base + j); nb(&s, 0x0B); nb(&s, 0x22); n_consti(&s, j + 1); nb(&s, 0x16); nb(&s, 0x20); nb(&s, 0x14); nb(&s, 0x20); }
    int v = 0; return cb_eval(&s, 4, &v) ? v : 0;    /* single value on top: extra 4 */
}
static void cb_copy_tmp(int slot) { Ncs s; n_void_main_begin(&s); n_push_slot(&s, CB_TMP); n_consti(&s, slot); n_action(&s, R_GETPLAYER, 0); n_action(&s, R_SETLOCALOBJECT, 3); cb_run("jbc_cp", &s); }
static void cb_freeze(int slot) { Ncs s; n_void_main_begin(&s); n_consti(&s, 1); n_push_slot(&s, slot); n_action(&s, 935, 2); cb_run("jbc_frz", &s); g_cbFreezes++; }
static void cb_set_health(int slot, int hp) { Ncs s; n_void_main_begin(&s); n_consti(&s, hp); n_push_slot(&s, slot); n_action(&s, 156, 2); cb_run("jbc_sh", &s); g_cbSets++; }
static void cb_kill(int slot) { Ncs s; n_void_main_begin(&s); n_constf(&s, 0.0f); n_consti(&s, 0); n_action(&s, 927, 1); n_consti(&s, 0); n_push_slot(&s, slot); n_action(&s, R_APPLYEFFECT, 4); cb_run("jbc_kill", &s); g_cbKills++; }
static void cb_hench_out(void) { Ncs s; n_void_main_begin(&s); n_consti(&s, 1); n_action(&s, 326 /* GetSelectedHenchman */, 0); n_action(&s, 1045, 2); cb_run("jbc_hf", &s); }
static void cb_move(int slot, float x, float y, float z, int run, int jump) {
    Ncs s; n_void_main_begin(&s); n_action(&s, R_CLEARALLACTIONS, 0);
    if (jump) { n_consti(&s, 1); n_loc(&s, x, y, z); n_action(&s, R_JUMPTOLOCATION, 2); }
    else { n_consti(&s, -1); n_consti(&s, run); n_loc(&s, x, y, z); n_action(&s, R_ACTIONMOVETOLOCATION, 3); }
    n_retn(&s); n_finish(&s); if (ms_store("jbc_act", s.b, s.n)) return;
    Ncs g; n_void_main_begin(&g); n_consts(&g, "jbc_act"); n_push_slot(&g, slot); n_action(&g, R_EXECUTESCRIPT, 2); cb_run("jbc_go", &g); g_cbMoves++;
}

/* One enumeration step per frame: candidate c = k*16+i. New hostile participants go into the table. */
static void cb_enum_step(int base, int guest) {
    int c = g_cbEnumC; g_cbEnumC = (g_cbEnumC + 1) % 64;
    { int jj = c % CB_MAX; int used = guest ? g_cbL[jj].used : g_cbH[jj].used;          /* prune one table entry per step */
      if (used && !cb_valid(base + jj)) { if (guest) { g_cbL[jj].used = 0; g_cbNL--; } else { g_cbH[jj].used = 0; g_cbNH--; } } }
    cb_pick(c / 16, c % 16);
    if (!cb_valid(CB_TMP)) return;
    int fac = 0; if (!cb_slot_int(CB_TMP, 713, &fac) || !(fac == 1 || fac == 3 || fac == 6)) return;
    if (cb_known(base, guest)) return;
    int j = 0; if (guest) { while (j < CB_MAX && g_cbL[j].used) j++; } else { while (j < CB_MAX && g_cbH[j].used) j++; }
    if (j >= CB_MAX) return;
    cb_copy_tmp(base + j);
    int32_t tag = 0; cb_tag(base + j, &tag); int x = 0, y = 0; cb_slot_pos(base + j, 0, &x); cb_slot_pos(base + j, 1, &y);
    if (guest) { CbLocal *L = &g_cbL[j]; memset(L, 0, sizeof *L); L->used = 1; L->tag = tag; L->sx = (int16_t)(x / 100); L->sy = (int16_t)(y / 100); L->match = -1; L->lastSet = -1; g_cbNL++; cb_freeze(base + j); }
    else { CbHost *H = &g_cbH[j]; memset(H, 0, sizeof *H); H->used = 1; H->tag = tag; H->sx = (int16_t)(x / 100); H->sy = (int16_t)(y / 100); g_cbNH++; }
}
static void cb_reset(void) { memset(g_cbH, 0, sizeof g_cbH); memset(g_cbL, 0, sizeof g_cbL); g_cbNH = g_cbNL = 0; g_cbEnumC = 0; g_cbHenchDone = 0; }

static void cb_host_tick(uint32_t now) {
    /* enumerate (one candidate per frame) for the first 4 s of a fight and then every 2 s for 1 s */
    if (now - g_cbEnumTick < 4000 || ((now - g_cbEnumTick) % 2000) < 1000) cb_enum_step(CB_HSLOT, 0);
    /* read one enemy per frame */
    for (int t = 0; t < CB_MAX; t++) {
        int j = (g_cbRead + t) % CB_MAX; if (!g_cbH[j].used) continue;
        g_cbRead = (j + 1) % CB_MAX; CbHost *H = &g_cbH[j]; int v;
        if (cb_slot_int(CB_HSLOT + j, 49, &v)) H->hp = v; if (cb_slot_int(CB_HSLOT + j, 140, &v)) H->dead = v;
        if (cb_slot_pos(CB_HSLOT + j, 0, &v)) H->x = v; if (cb_slot_pos(CB_HSLOT + j, 1, &v)) H->y = v; if (cb_slot_pos(CB_HSLOT + j, 2, &v)) H->z = v;
        break;
    }
    if (now - g_cbSnapTick >= 250) {
        g_cbSnapTick = now; uint8_t p[8 + CB_MAX * 26]; int n = 0; memcpy(p, "JEF1", 4);
        for (int j = 0; j < CB_MAX; j++) if (g_cbH[j].used) {
            uint8_t *e = p + 8 + 26 * n++; CbHost *H = &g_cbH[j]; int16_t hp = (int16_t)H->hp;
            e[0] = (uint8_t)j; e[1] = (uint8_t)H->dead; memcpy(e + 2, &hp, 2); memcpy(e + 4, &H->tag, 4); memcpy(e + 8, &H->sx, 2); memcpy(e + 10, &H->sy, 2);
            memcpy(e + 12, &H->x, 4); memcpy(e + 16, &H->y, 4); memcpy(e + 20, &H->z, 4); e[24] = e[25] = 0;
        }
        p[4] = (uint8_t)n; p[5] = p[6] = p[7] = 0;
        if (n) { steam_send((const char *)p, 8 + 26 * n); g_cbSnaps++; }
    }
}
/* host: a guest hit enemy j for d */
static void cb_on_damage(const uint8_t *p, int n) {
    if (!g_steamHost || n < 16) return;
    uint32_t j; int32_t tag, d; memcpy(&j, p + 4, 4); memcpy(&tag, p + 8, 4); memcpy(&d, p + 12, 4);
    if (j >= CB_MAX || !g_cbH[j].used || g_cbH[j].tag != tag || d <= 0 || !player_exists()) return;
    int hp = 0; if (!cb_slot_int(CB_HSLOT + j, 49, &hp)) return;
    int nh = hp - d; if (nh < 0) nh = 0;
    cb_set_health(CB_HSLOT + j, nh); if (nh == 0) cb_kill(CB_HSLOT + j);
    g_cbH[j].hp = nh; g_cbDmgApplied++;
}
/* guest: snapshot from the host */
static void cb_on_snapshot(const char *pkt, int r) {
    if (g_steamHost || r < 8) return;
    int n = (uint8_t)pkt[4]; if (n > CB_MAX || 8 + 26 * n > r) return;
    memset(g_cbR, 0, sizeof g_cbR);
    for (int k = 0; k < n; k++) {
        const uint8_t *e = (const uint8_t *)pkt + 8 + 26 * k; int j = e[0]; if (j >= CB_MAX) continue;
        CbRemote *R = &g_cbR[j]; int16_t hp; R->valid = 1; R->dead = e[1]; memcpy(&hp, e + 2, 2); R->hp = hp; memcpy(&R->tag, e + 4, 4);
        memcpy(&R->sx, e + 8, 2); memcpy(&R->sy, e + 10, 2); memcpy(&R->x, e + 12, 4); memcpy(&R->y, e + 16, 4); memcpy(&R->z, e + 20, 4);
    }
    g_cbSnapsIn++;
}
static void cb_guest_tick(uint32_t now) {
    if (!g_cbHenchDone) { cb_hench_out(); g_cbHenchDone = 1; }
    if (now - g_cbEnumTick < 4000 || ((now - g_cbEnumTick) % 2000) < 1000) cb_enum_step(CB_GSLOT, 1);
    /* match: host enemy j <-> unmatched local enemy with the same tag, nearest start position (<= 8 m) */
    for (int j = 0; j < CB_MAX; j++) {
        if (!g_cbR[j].valid) continue; int have = 0;
        for (int l = 0; l < CB_MAX; l++) if (g_cbL[l].used && g_cbL[l].match == j) { have = 1; break; }
        if (have) continue;
        int best = -1; int32_t bd = 80 * 80;
        for (int l = 0; l < CB_MAX; l++) { CbLocal *L = &g_cbL[l]; if (!L->used || L->match >= 0 || L->tag != g_cbR[j].tag) continue;
            int32_t dx = L->sx - g_cbR[j].sx, dy = L->sy - g_cbR[j].sy, d2 = dx * dx + dy * dy; if (d2 <= bd) { bd = d2; best = l; } }
        if (best >= 0) g_cbL[best].match = j;
    }
    /* apply to one matched local enemy per frame */
    static int rr; for (int t = 0; t < CB_MAX; t++) {
        int l = (rr + t) % CB_MAX; CbLocal *L = &g_cbL[l]; if (!L->used || L->match < 0 || !g_cbR[L->match].valid) continue;
        rr = (l + 1) % CB_MAX; CbRemote *R = &g_cbR[L->match];
        if (R->tag != L->tag) { L->match = -1; break; } int slot = CB_GSLOT + l, hp = 0, dead = 0;
        if (!cb_slot_int(slot, 49, &hp) || !cb_slot_int(slot, 140, &dead)) break;
        if (dead) { L->dead = 1; break; }
        if (R->dead) { cb_kill(slot); L->dead = 1; break; }
        if (L->lastSet >= 0 && hp < L->lastSet) {                    /* the guest hit it */
            uint8_t q[16]; uint32_t jj = (uint32_t)L->match; int32_t d = L->lastSet - hp; memcpy(q, "JEDM", 4); memcpy(q + 4, &jj, 4); memcpy(q + 8, &R->tag, 4); memcpy(q + 12, &d, 4);
            if (xf_send(q, 16)) g_cbDmgSent++; L->lastSet = hp; L->pend = now; break;
        }
        if (now - L->pend > 1500 && hp != R->hp && R->hp > 0) { cb_set_health(slot, R->hp); L->lastSet = R->hp; break; }
        if (L->lastSet < 0) L->lastSet = hp;
        if (now - L->moveTick > 500) {
            L->moveTick = now; int x = 0, y = 0;
            if (cb_slot_pos(slot, 0, &x) && cb_slot_pos(slot, 1, &y)) {
                float dx = (R->x - x) / 1000.0f, dy = (R->y - y) / 1000.0f, d2 = dx * dx + dy * dy;
                if (d2 > 10.0f * 10.0f) cb_move(slot, R->x / 1000.0f, R->y / 1000.0f, R->z / 1000.0f, 0, 1);
                else if (d2 > 1.5f * 1.5f) cb_move(slot, R->x / 1000.0f, R->y / 1000.0f, R->z / 1000.0f, d2 > 4.0f * 4.0f, 0);
            }
        }
        break;
    }
}
/* called every frame with a player present; tune 128 = off (mutation runs) */
static void cb_tick(void) {
    if (!g_useSteam || !g_steamOn || (g_tune & 128)) return;
    uint32_t now = GetTickCount();
    static uint32_t lastCheck; if (now - lastCheck >= 500) { lastCheck = now; int f = cb_in_fight();
        if (f && !g_cbInFight) { cb_reset(); g_cbEnumTick = now; logline(g_steamHost ? "combat: fight started (host)" : "combat: fight started (guest)"); }
        if (!f && g_cbInFight) logline("combat: fight over");
        g_cbInFight = f; }
    if (!g_cbInFight) return;
    if (g_steamHost) cb_host_tick(now); else cb_guest_tick(now);
}
static void cb_status(char *out) {
    char a[16];
    s_cat(out, ",\"combat\":{\"inFight\":"); s_cat(out, g_cbInFight ? "true" : "false");
    s_cat(out, ",\"host\":["); int f = 1;
    for (int j = 0; j < CB_MAX; j++) if (g_cbH[j].used) { if (!f) s_cat(out, ","); f = 0; s_cat(out, "["); i2s(a, j); s_cat(out, a); s_cat(out, ","); i2s(a, g_cbH[j].hp); s_cat(out, a); s_cat(out, ","); i2s(a, g_cbH[j].dead); s_cat(out, a); s_cat(out, "]"); }
    s_cat(out, "],\"local\":["); f = 1;
    for (int l = 0; l < CB_MAX; l++) if (g_cbL[l].used) { if (!f) s_cat(out, ","); f = 0; s_cat(out, "["); i2s(a, l); s_cat(out, a); s_cat(out, ","); i2s(a, g_cbL[l].match); s_cat(out, a); s_cat(out, ","); i2s(a, g_cbL[l].lastSet); s_cat(out, a); s_cat(out, ","); i2s(a, g_cbL[l].dead); s_cat(out, a); s_cat(out, "]"); }
    s_cat(out, "],\"snaps\":"); u2s(a, g_cbSnaps); s_cat(out, a); s_cat(out, ",\"snapsIn\":"); u2s(a, g_cbSnapsIn); s_cat(out, a);
    s_cat(out, ",\"dmgSent\":"); u2s(a, g_cbDmgSent); s_cat(out, a); s_cat(out, ",\"dmgApplied\":"); u2s(a, g_cbDmgApplied); s_cat(out, a);
    s_cat(out, ",\"kills\":"); u2s(a, g_cbKills); s_cat(out, a); s_cat(out, ",\"sets\":"); u2s(a, g_cbSets); s_cat(out, a);
    s_cat(out, ",\"moves\":"); u2s(a, g_cbMoves); s_cat(out, a); s_cat(out, ",\"freezes\":"); u2s(a, g_cbFreezes); s_cat(out, a); s_cat(out, "}");
}
