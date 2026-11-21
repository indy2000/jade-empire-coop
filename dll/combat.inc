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
typedef struct { int used; int32_t tag; int16_t sx, sy; int hp, dead; int32_t x, y, z; uint32_t oid; int onPup; } CbHost;
typedef struct { int used; int32_t tag; int16_t sx, sy; int hp, dead; int32_t x, y, z; int match, lastSet; uint32_t pend, moveTick, oid; int aiOn; } CbLocal;
typedef struct { int valid; int32_t tag; int16_t sx, sy; int hp, dead; int32_t x, y, z; int onPup; } CbRemote;
static CbHost g_cbH[CB_MAX]; static CbLocal g_cbL[CB_MAX]; static CbRemote g_cbR[CB_MAX];
static int g_cbInFight, g_cbEnumC, g_cbRead, g_cbNH, g_cbNL, g_cbHenchDone;
static uint8_t g_cbFightId, g_cbRid; static uint32_t g_cbRtick;
/* 0.11.0: host enemies fight the guest's puppet (RE, NOTES "RE: how an enemy picks its target").
   A creature's combat target struct is at +0x58c; +0x5a4 (struct+0x18) is a wanted target that the
   AI (FUN_004709c0 / FUN_00470470) takes when FUN_004bc760(enemy, wanted) passes. It failed for the
   puppet only on the relation (neutral, 4); with ChangeToStandardFaction(puppet, FRIENDLY1) it is 2
   and the enemy attacks the puppet (owner saw it, puppet health went down). So: the host gives each
   enemy one of the two players (the nearer one, at least one enemy each when there are two), writes
   the puppet's id into +0x5a4 every frame for the puppet's enemies, measures the puppet's health and
   sends what it lost to the guest ('JEPH'), who takes it off its own player. */
#define CB_ID_INVALID (*(volatile uint32_t *)0x0075B394)
static uint32_t g_cbMyOid, g_cbHitTick; static int g_cbMyHp = -1, g_cbHealReset; static uint32_t g_cbHeals, g_cbHealed;
static uint32_t g_cbPupOid, g_cbPupTick, g_cbAssignTick, g_cbPupHits, g_cbPupDmg, g_cbHitsIn; static int g_cbPupHp = -1, g_cbPupMax;
static uint32_t g_cbSnapTick, g_cbEnumTick, g_cbSnaps, g_cbSnapsIn, g_cbDmgSent, g_cbDmgApplied, g_cbKills, g_cbSets, g_cbMoves, g_cbFreezes;

static uint32_t cb_watch_addr(int i) { return i ? (uint32_t)(uintptr_t)&g_cbH[0].used : (uint32_t)(uintptr_t)&g_cbInFight; }
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
/* exists: GetIsObjectValid read 0 for a live enemy (faction 1, hp 25), so also accept a faction or health */
/* 1 exists, 0 gone, -1 could not tell (a script did not run: the VM was busy) */
static int cb_valid(int slot) { Ncs s; n_int_main_begin(&s); n_push_slot(&s, slot); n_action(&s, R_GETISOBJECTVALID, 1); int v = 0;
    if (!cb_eval(&s, 4, &v)) return -1; if (v) return 1;
    int f = 0, h = 0; if (!cb_slot_int(slot, 713, &f) || !cb_slot_int(slot, 49, &h)) return -1; return f != 0 || h != 0; }
/* index+1 of the table slot (base..base+15) holding the same object as CB_TMP, 0 if none (EQUALOO 0x0B 0x22) */
static int cb_known(int base, int guest) {
    Ncs s; n_int_main_begin(&s); n_consti(&s, 0);
    for (int j = 0; j < CB_MAX; j++) { if (!(guest ? g_cbL[j].used : g_cbH[j].used)) continue; n_push_slot(&s, CB_TMP); n_push_slot(&s, base + j); nb(&s, 0x0B); nb(&s, 0x22); n_consti(&s, j + 1); nb(&s, 0x16); nb(&s, 0x20); nb(&s, 0x14); nb(&s, 0x20); }
    int v = 0; return cb_eval(&s, 4, &v) ? v : -1;   /* single value on top: extra 4; -1 = could not tell */
}
static void cb_copy_tmp(int slot) { Ncs s; n_void_main_begin(&s); n_push_slot(&s, CB_TMP); n_consti(&s, slot); n_action(&s, R_GETPLAYER, 0); n_action(&s, R_SETLOCALOBJECT, 3); cb_run("jbc_cp", &s); }
static void cb_freeze(int slot) { Ncs s; n_void_main_begin(&s); n_consti(&s, 1); n_push_slot(&s, slot); n_action(&s, 935, 2); cb_run("jbc_frz", &s); g_cbFreezes++; }
static void cb_ai(int slot, int on) { Ncs s; n_void_main_begin(&s); n_consti(&s, on ? 0 : 1); n_push_slot(&s, slot); n_action(&s, 935, 2); cb_run("jbc_ai", &s); }
static void cb_set_health(int slot, int hp) { Ncs s; n_void_main_begin(&s); n_consti(&s, hp); n_push_slot(&s, slot); n_action(&s, 156, 2); cb_run("jbc_sh", &s); g_cbSets++; }
static void cb_kill(int slot) { Ncs s; n_void_main_begin(&s); n_constf(&s, 0.0f); n_consti(&s, 0); n_action(&s, 927, 1); n_consti(&s, 0); n_push_slot(&s, slot); n_action(&s, R_APPLYEFFECT, 4); cb_run("jbc_kill", &s); g_cbKills++; }
/* object id of the object in a slot: ObjectToString (#272) is its id in hex; parsed in NCS */
static int cb_oid(int slot, uint32_t *out) {
    Ncs s; n_int_main_begin(&s); n_consti(&s, 0); n_consti(&s, 8); n_consts(&s, "00000000"); n_push_slot(&s, slot);
    n_action(&s, 272, 1); n_action(&s, 61, 1); nb(&s, 0x14); nb(&s, 0x23); n_action(&s, 62, 2);         /* h, right8("00000000" + lower(hex)) */
    for (int i = 0; i < 8; i++) {
        n_consti(&s, 1); n_consti(&s, i); n_cptopsp(&s, -12, 4); n_action(&s, 65, 3); n_consts(&s, "0123456789abcdef"); n_action(&s, 66, 2);
        n_cptopsp(&s, -12, 4); n_consti(&s, 16); nb(&s, 0x16); nb(&s, 0x20); nb(&s, 0x14); nb(&s, 0x20); n_cpdownsp(&s, -12, 4); n_movsp(&s, -4);
    }
    n_cptopsp(&s, -8, 4); int v = 0; if (!cb_eval(&s, 12, &v)) return 0; *out = (uint32_t)v; return 1;
}
static uintptr_t cb_creature(uint32_t oid) {
    int *app = *G_APP; void *srv = app ? *(void **)((char *)app + 0xA0) : 0; uint32_t id = oid;
    uintptr_t c = (srv && oid && oid != CB_ID_INVALID) ? gcall(0x004611b0, (uint32_t)(uintptr_t)srv, 1, (uint32_t)(uintptr_t)&id, 0, 0) : 0;
    return c && mem_readable(c + 0x588, 0x30) ? c : 0;
}
static void cb_pup_setup(void) {   /* the puppet becomes a valid target: FRIENDLY1, AI off, cannot die here */
    Ncs a; n_void_main_begin(&a); n_consti(&a, 2); n_push_puppet(&a); n_action(&a, 412, 2); cb_run("jbc_pf", &a);
    Ncs b; n_void_main_begin(&b); n_consti(&b, 1); n_push_puppet(&b); n_action(&b, 935, 2); cb_run("jbc_pa", &b);
    Ncs c; n_void_main_begin(&c); n_consti(&c, 1); n_push_puppet(&c); n_action(&c, 716, 2); cb_run("jbc_pm", &c);
}
static int cb_pup_int(uint16_t r, int *out) { Ncs s; n_int_main_begin(&s); n_push_puppet(&s); n_action(&s, r, 1); return cb_eval(&s, 4, out); }
static void cb_pup_refill(void) { Ncs s; n_void_main_begin(&s); n_push_puppet(&s); n_action(&s, 50, 1); n_push_puppet(&s); n_action(&s, 156, 2); cb_run("jbc_pr", &s); }
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
      if (used && cb_valid(base + jj) == 0) { if (guest) { g_cbL[jj].used = 0; g_cbNL--; } else { g_cbH[jj].used = 0; g_cbNH--; } } }
    cb_pick(c / 16, c % 16);
    if (cb_valid(CB_TMP) != 1) return;
    int fac = 0; if (!cb_slot_int(CB_TMP, 713, &fac) || !(fac == 1 || fac == 3 || fac == 6)) return;
    if (cb_known(base, guest) != 0) return;
    int j = 0; if (guest) { while (j < CB_MAX && g_cbL[j].used) j++; } else { while (j < CB_MAX && g_cbH[j].used) j++; }
    if (j >= CB_MAX) return;
    cb_copy_tmp(base + j);
    int32_t tag = 0; cb_tag(base + j, &tag); int x = 0, y = 0; cb_slot_pos(base + j, 0, &x); cb_slot_pos(base + j, 1, &y);
    if (guest) { CbLocal *L = &g_cbL[j]; memset(L, 0, sizeof *L); L->used = 1; L->tag = tag; L->sx = (int16_t)(x / 100); L->sy = (int16_t)(y / 100); L->match = -1; L->lastSet = -1; cb_oid(base + j, &L->oid); g_cbNL++; cb_freeze(base + j); }
    else { CbHost *H = &g_cbH[j]; memset(H, 0, sizeof *H); H->used = 1; H->tag = tag; H->sx = (int16_t)(x / 100); H->sy = (int16_t)(y / 100); cb_oid(base + j, &H->oid); g_cbNH++; }
}
/* 0.10.1: also forget the host's last snapshot: after a reload the guest matched its new enemies to
   the dead ones of the previous fight (same tags, same places) and killed them */
static void cb_reset(void) { memset(g_cbH, 0, sizeof g_cbH); memset(g_cbL, 0, sizeof g_cbL); memset(g_cbR, 0, sizeof g_cbR); g_cbRtick = 0; g_cbPupOid = 0; g_cbPupHp = -1; g_cbPupTick = 0; g_cbMyHp = -1; g_cbMyOid = 0; g_cbHealReset = 1; g_cbNH = g_cbNL = 0; g_cbEnumC = 0; g_cbHenchDone = 0; }

static void cb_send_hits(int d) { uint8_t q[12]; memcpy(q, "JEPH", 4); memcpy(q + 4, &d, 4); memset(q + 8, 0, 4); if (xf_send(q, 12)) g_cbPupHits++; g_cbPupDmg += (uint32_t)d; }
static void cb_host_puppet(uint32_t now) {
    if (g_tune & 64) return;                                   /* mutation runs: enemies never fight the puppet */
    if (!g_puppetState) { g_cbPupOid = 0; return; }           /* no live puppet */
    /* (both roles) the puppet stays a valid target; its hits are forwarded by the host only */
    if (!g_cbPupOid || now - g_cbPupTick > 3000) {           /* (re)find it: it can be respawned */
        g_cbPupTick = now; uint32_t o = 0; if (cb_oid(NET_SLOT, &o) && o != CB_ID_INVALID) { if (o != g_cbPupOid) { g_cbPupOid = o; cb_pup_setup(); g_cbPupHp = -1; } } }
    if (!g_cbPupOid) return;
    /* the puppet's health: what it lost goes to the guest, then it is refilled */
    { int hp = 0; if (cb_pup_int(49, &hp)) {
        if (g_cbPupHp < 0) { cb_pup_int(50, &g_cbPupMax); cb_pup_refill(); g_cbPupHp = g_cbPupMax; }
        else if (hp < g_cbPupHp) { if (g_steamHost) cb_send_hits(g_cbPupHp - hp); cb_pup_refill(); g_cbPupHp = g_cbPupMax; } } }
    if (!g_steamHost) return;
    /* who fights whom, every second: each enemy takes the nearer player; with two or more enemies,
       at least one each (the one nearest to the player who has none) */
    if (now - g_cbAssignTick >= 1000) {
        g_cbAssignTick = now; int px = 0, py = 0;
        { Ncs a; n_int_main_begin(&a); n_action(&a, R_GETPLAYER, 0); n_pos_component(&a, 0); cb_eval(&a, 16, &px); }
        { Ncs b; n_int_main_begin(&b); n_action(&b, R_GETPLAYER, 0); n_pos_component(&b, 1); cb_eval(&b, 16, &py); }
        float qx = g_rxm / 1000.0f, qy = g_rym / 1000.0f; int nA = 0, nP = 0, live = 0;
        for (int j = 0; j < CB_MAX; j++) { CbHost *H = &g_cbH[j]; if (!H->used || H->dead || (!H->x && !H->y)) continue; live++;
            float ex = H->x / 1000.0f, ey = H->y / 1000.0f, da = (ex - px / 1000.0f) * (ex - px / 1000.0f) + (ey - py / 1000.0f) * (ey - py / 1000.0f),
                  dp = (ex - qx) * (ex - qx) + (ey - qy) * (ey - qy);
            if (H->onPup ? da + 9.0f < dp : dp + 9.0f < da) H->onPup = !H->onPup;   /* 3 m hysteresis (squared-ish) */
            if (H->onPup) nP++; else nA++; }
        if (live >= 2 && (nA == 0 || nP == 0)) {
            int want = nP == 0, best = -1; float bd = 1e30f;
            for (int j = 0; j < CB_MAX; j++) { CbHost *H = &g_cbH[j]; if (!H->used || H->dead) continue;
                float ex = H->x / 1000.0f, ey = H->y / 1000.0f, tx = want ? qx : px / 1000.0f, ty = want ? qy : py / 1000.0f, d = (ex - tx) * (ex - tx) + (ey - ty) * (ey - ty);
                if (d < bd) { bd = d; best = j; } }
            if (best >= 0) g_cbH[best].onPup = want;
        }
    }
    /* every frame: the puppet's enemies want the puppet; the others want nothing (the AI then takes the player) */
    for (int j = 0; j < CB_MAX; j++) { CbHost *H = &g_cbH[j]; if (!H->used || H->dead || !H->oid) continue;
        uintptr_t c = cb_creature(H->oid); if (!c) continue; volatile uint32_t *want = (volatile uint32_t *)(c + 0x5a4);
        if (H->onPup) *want = g_cbPupOid; else if (*want == g_cbPupOid) *want = CB_ID_INVALID; }
}
/* guest: the host's enemies hit our puppet there -> take it off our player */
static int cb_my_hp(void) { int hp = -1; Ncs a; n_int_main_begin(&a); n_action(&a, R_GETPLAYER, 0); n_action(&a, 49, 1); return cb_eval(&a, 4, &hp) ? hp : -1; }
static void cb_my_minhp(int v) { Ncs a; n_void_main_begin(&a); n_consti(&a, v); n_action(&a, R_GETPLAYER, 0); n_action(&a, 716, 2); cb_run("jbc_mm", &a); }
static void cb_on_hits(const uint8_t *p, int n) {
    if (g_steamHost || n < 8 || !player_exists()) return; int32_t d; memcpy(&d, p + 4, 4); if (d <= 0 || d > 100000) return;
    int before = cb_my_hp(); if (before >= 0 && d >= before && !(g_tune & 256)) cb_my_minhp(0);   /* the host's hit is lethal: let it kill (tune 256: tests keep the player alive) */
    /* EffectDamageHealth (#771), instant: SetHealth on the player changed what GetHealth reads but not the
       health bar, and the player did not die at 0 (owner, copy B); the damage effect moves the bar. */
    Ncs b; n_void_main_begin(&b); n_constf(&b, 0.0f); n_consti(&b, d); n_action(&b, 771, 1); n_consti(&b, 0); n_action(&b, R_GETPLAYER, 0); n_action(&b, R_APPLYEFFECT, 4); cb_run("jbc_ph", &b);
    /* the effect shows in GetHealth a little later: reading it right away gave the old value, and the
       heal-back then undid the host's hit (seen: B 70 with 57 host damage). Expect before - d instead,
       and don't judge for 600 ms */
    g_cbHitsIn++; if (before >= 0) { g_cbMyHp = before - d; if (g_cbMyHp < 1) g_cbMyHp = 1; } else g_cbMyHp = -1; g_cbHitTick = GetTickCount();
}
/* guest: our enemies fight locally (animations), but only the host's hits count: local hits are healed
   back (EffectModifyHealth #746, instant), and a minimum health of 1 keeps a local hit from killing us */
static uint32_t g_cbHealTick; static int g_cbHealLogs;
static void cb_log3(const char *what, int a, int b, int c2);
static void cb_guest_heal(void) {
    /* 0.11.0: one heal, then 300 ms before looking again: in one run 790 heals of exactly 20 were counted
       for a few hits, so the heal seemed to show up in GetHealth only a while later (unknown) */
    if (g_cbHealReset) { g_cbHealReset = 0; g_cbHealLogs = 0; }
    uint32_t now = GetTickCount(); if (now - g_cbHealTick < 300 || now - g_cbHitTick < 600) return;
    int hp = cb_my_hp(); if (hp < 0) return;
    if (hp < g_cbMyHp && g_cbHealLogs < 30) { g_cbHealLogs++; cb_log3("heal back: hp read, expected, ms since last heal", hp, g_cbMyHp, (int)(now - g_cbHealTick)); }
    if (g_cbMyHp < 0) { g_cbMyHp = hp; cb_my_minhp(1); return; }
    if (hp < g_cbMyHp) { int d = g_cbMyHp - hp; Ncs b; n_void_main_begin(&b); n_constf(&b, 0.0f); n_consti(&b, d); n_action(&b, 746, 1); n_consti(&b, 0); n_action(&b, R_GETPLAYER, 0); n_action(&b, R_APPLYEFFECT, 4); cb_run("jbc_hl", &b);
                         g_cbHeals++; g_cbHealed += (uint32_t)d; g_cbHealTick = now; }
    else g_cbMyHp = hp;   /* healed by the game (potions, regeneration): accept */
}
static void cb_host_tick(uint32_t now) {
    cb_host_puppet(now);
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
            memcpy(e + 12, &H->x, 4); memcpy(e + 16, &H->y, 4); memcpy(e + 20, &H->z, 4); e[24] = (uint8_t)H->onPup; e[25] = 0;
        }
        p[4] = (uint8_t)n; p[5] = g_cbFightId; p[6] = p[7] = 0;
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
    if (!g_cbInFight) return;                     /* a snapshot only counts during our own fight */
    if ((uint8_t)pkt[5] != g_cbRid) { g_cbRid = (uint8_t)pkt[5]; for (int l = 0; l < CB_MAX; l++) g_cbL[l].match = -1; }   /* host's new fight: match again */
    memset(g_cbR, 0, sizeof g_cbR); g_cbRtick = GetTickCount();
    for (int k = 0; k < n; k++) {
        const uint8_t *e = (const uint8_t *)pkt + 8 + 26 * k; int j = e[0]; if (j >= CB_MAX) continue;
        CbRemote *R = &g_cbR[j]; int16_t hp; R->valid = 1; R->dead = e[1]; memcpy(&hp, e + 2, 2); R->hp = hp; memcpy(&R->tag, e + 4, 4);
        memcpy(&R->sx, e + 8, 2); memcpy(&R->sy, e + 10, 2); memcpy(&R->x, e + 12, 4); memcpy(&R->y, e + 16, 4); memcpy(&R->z, e + 20, 4); R->onPup = e[24];
    }
    g_cbSnapsIn++;
}
static void cb_guest_tick(uint32_t now) {
    if (!g_cbHenchDone) { cb_hench_out(); g_cbHenchDone = 1; }
    cb_host_puppet(now);                                   /* the host's puppet here is a valid target too */
    if (!(g_tune & 64)) cb_guest_heal();
    if (now - g_cbEnumTick < 4000 || ((now - g_cbEnumTick) % 2000) < 1000) cb_enum_step(CB_GSLOT, 1);
    if (!g_cbRtick || now - g_cbRtick > 1500) return;   /* no fresh snapshot from the host's current fight */
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
        if (R->dead) { if (!dead) cb_kill(slot); L->dead = 1; break; }
        if (dead) {                                                   /* 0.10.1: the guest's killing blow reaches the host */
            if (!L->dead) { uint8_t q[16]; uint32_t jj = (uint32_t)L->match; int32_t d = 1000000; memcpy(q, "JEDM", 4); memcpy(q + 4, &jj, 4); memcpy(q + 8, &R->tag, 4); memcpy(q + 12, &d, 4);
                            if (xf_send(q, 16)) g_cbDmgSent++; }
            L->dead = 1; break; }
        if (L->lastSet >= 0 && hp < L->lastSet) {                    /* the guest hit it */
            uint8_t q[16]; uint32_t jj = (uint32_t)L->match; int32_t d = L->lastSet - hp; memcpy(q, "JEDM", 4); memcpy(q + 4, &jj, 4); memcpy(q + 8, &R->tag, 4); memcpy(q + 12, &d, 4);
            if (xf_send(q, 16)) g_cbDmgSent++; L->lastSet = hp; L->pend = now; break;
        }
        if (now - L->pend > 1500 && hp != R->hp && R->hp > 0) { cb_set_health(slot, R->hp); L->lastSet = R->hp; break; }
        if (L->lastSet < 0) L->lastSet = hp;
        /* 0.11.0: same opponent as on the host, own AI (so it attacks with animations) */
        if (!(g_tune & 64)) {
            if (!L->aiOn) { cb_ai(slot, 1); L->aiOn = 1; }
            uintptr_t c = cb_creature(L->oid);
            if (c) { volatile uint32_t *want = (volatile uint32_t *)(c + 0x5a4);
                if (!R->onPup && g_cbPupOid) *want = g_cbPupOid;              /* host fights it there: here it fights the host's puppet */
                else if (*want == g_cbPupOid) *want = CB_ID_INVALID; }        /* it fights us: the AI takes our player */
        }
        if (now - L->moveTick > 500) {
            L->moveTick = now; int x = 0, y = 0;
            if (cb_slot_pos(slot, 0, &x) && cb_slot_pos(slot, 1, &y)) {
                float dx = (R->x - x) / 1000.0f, dy = (R->y - y) / 1000.0f, d2 = dx * dx + dy * dy;
                float near = L->aiOn ? 6.0f : 1.5f;                            /* its own AI moves it; correct only when far off */
                if (d2 > 10.0f * 10.0f) cb_move(slot, R->x / 1000.0f, R->y / 1000.0f, R->z / 1000.0f, 0, 1);
                else if (d2 > near * near) cb_move(slot, R->x / 1000.0f, R->y / 1000.0f, R->z / 1000.0f, d2 > 4.0f * 4.0f, 0);
            }
        }
        break;
    }
}
/* called every frame with a player present; tune 128 = off (mutation runs) */
static int cb_used(void) { int n = 0; for (int j = 0; j < CB_MAX; j++) n += g_steamHost ? g_cbH[j].used : g_cbL[j].used; return n; }
static void cb_log3(const char *what, int a, int b, int c2) { char m[200], t[16]; s_cpy(m, "combat: "); s_cat(m, what); s_cat(m, " ");
    i2s(t, a); s_cat(m, t); s_cat(m, " "); i2s(t, b); s_cat(m, t); s_cat(m, " "); i2s(t, c2); s_cat(m, t); s_cat(m, " stage "); s_cat(m, g_stage ? (const char *)g_stage : "?"); logline(m); }
static void cb_fight_end(void) {   /* host: no wanted targets left behind */
    for (int j = 0; j < CB_MAX; j++) { CbHost *H = &g_cbH[j]; if (!H->used || !H->oid) continue; uintptr_t c = cb_creature(H->oid);
        if (c && g_cbPupOid && *(volatile uint32_t *)(c + 0x5a4) == g_cbPupOid) *(volatile uint32_t *)(c + 0x5a4) = CB_ID_INVALID; H->onPup = 0; }
}
static void cb_tick(void) {
    if (!g_useSteam || !g_steamOn || (g_tune & 128)) return;
    uint32_t now = GetTickCount();
    { extern volatile int32_t g_gcDelta; extern volatile uint32_t g_gcDeltaFn; extern volatile int g_gcBad; static int seen;
      if (g_gcBad != seen && seen < 20) { seen = g_gcBad; cb_log3("GCALL stack off: fn, bytes, count", (int)g_gcDeltaFn, g_gcDelta, g_gcBad); } }
    if (g_cbInFight != g_cbShadow) { cb_log3("FLAG CHANGED OUTSIDE cb_tick: shadow, now, table entries", g_cbShadow, g_cbInFight, cb_used()); g_cbInFight = g_cbShadow; }
    static int lastUsed; int u0 = cb_used(); if (lastUsed && !u0) cb_log3("table emptied before this tick: had, now, inFight", lastUsed, u0, g_cbInFight);
    static uint32_t lastCheck; if (now - lastCheck >= 500) { lastCheck = now; int f = cb_in_fight();
        if (f && !g_cbInFight) { cb_reset(); g_cbEnumTick = now; g_cbFightId++; logline(g_steamHost ? "combat: fight started (host)" : "combat: fight started (guest)"); }
        if (!f && g_cbInFight) { if (g_steamHost) cb_fight_end(); logline("combat: fight over"); }
        g_cbInFight = f; g_cbShadow = f; }
    lastUsed = cb_used();
    if (!g_cbInFight) { lastUsed = cb_used(); return; }
    if (g_steamHost) cb_host_tick(now); else cb_guest_tick(now);
    lastUsed = cb_used();
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
    s_cat(out, ",\"moves\":"); u2s(a, g_cbMoves); s_cat(out, a);
    s_cat(out, ",\"onPuppet\":["); f = 1; for (int j = 0; j < CB_MAX; j++) if (g_cbH[j].used && g_cbH[j].onPup) { if (!f) s_cat(out, ","); f = 0; i2s(a, j); s_cat(out, a); } s_cat(out, "]");
    s_cat(out, ",\"pupOid\":"); u2s(a, g_cbPupOid); s_cat(out, a); s_cat(out, ",\"pupHits\":"); u2s(a, g_cbPupHits); s_cat(out, a);
    s_cat(out, ",\"pupDmg\":"); u2s(a, g_cbPupDmg); s_cat(out, a); s_cat(out, ",\"heals\":"); u2s(a, g_cbHeals); s_cat(out, a); s_cat(out, ",\"healed\":"); u2s(a, g_cbHealed); s_cat(out, a); s_cat(out, ",\"hitsIn\":"); u2s(a, g_cbHitsIn); s_cat(out, a); s_cat(out, ",\"freezes\":"); u2s(a, g_cbFreezes); s_cat(out, a); s_cat(out, "}");
}
