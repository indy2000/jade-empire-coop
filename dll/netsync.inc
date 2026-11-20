/* netsync.inc — game-to-game player sync for jebridge (included by jebridge.c).
 *
 * Each game sends its own player's position over UDP ~10 times a second. The receiving
 * game shows that player as a puppet creature: spawned from an area-safe template,
 * resurrected (CreateObject creatures are born dead in Jade Empire), then driven with
 * ActionMoveToLocation, or jumped when far away. All game calls run on the main thread
 * (net_tick() is called from the PeekMessageA hook).
 *
 * Enabled only when <game dir>\bridge\net.txt exists. Format, one setting per line:
 *   local <udp port>              e.g. local 47400
 *   peer <ipv4> <udp port>        e.g. peer 127.0.0.1 47401
 *   template <resref>             puppet creature template (must be loaded in the area)
 *
 * Wire format (little endian), 24 bytes:
 *   'J','E','C','1', uint32 seq, int32 x_mm, int32 y_mm, int32 z_mm, uint32 flags (bit0 = player valid)
 *   (millimetres as integers: no float handling on the hot path)
 */

/* ---- winsock bits not declared above ---- */
__declspec(dllimport) int __stdcall sendto(SOCKET, const char*, int, int, const void*, int);
__declspec(dllimport) int __stdcall recvfrom(SOCKET, char*, int, int, void*, int*);
__declspec(dllimport) int __stdcall ioctlsocket(SOCKET, long, unsigned long*);

/* ---- tiny NCS builder (same encodings as client/ncs.py) ---- */
/* 0.9.1: 512 -> 4096 bytes; an overflowing script is refused (n = 0), it was cut off silently before */
typedef struct { uint8_t b[4096]; uint32_t n; int over; } Ncs;
static void nb(Ncs *s, uint8_t v) { if (s->n < sizeof s->b) s->b[s->n++] = v; else s->over = 1; }
static void nbe32(Ncs *s, uint32_t v) { nb(s, v >> 24); nb(s, v >> 16); nb(s, v >> 8); nb(s, v); }
static void nbe16(Ncs *s, uint16_t v) { nb(s, v >> 8); nb(s, v); }
static void n_consti(Ncs *s, int32_t v) { nb(s, 0x04); nb(s, 0x03); nbe32(s, (uint32_t)v); }
static void n_constf(Ncs *s, float f) { union { float f; uint32_t u; } c; c.f = f; nb(s, 0x04); nb(s, 0x04); nbe32(s, c.u); }
static void n_consts(Ncs *s, const char *t) { uint16_t l = (uint16_t)s_len(t); nb(s, 0x04); nb(s, 0x05); nbe16(s, l); for (uint16_t i = 0; i < l; i++) nb(s, (uint8_t)t[i]); }
static void n_action(Ncs *s, uint16_t r, uint8_t argc) { nb(s, 0x05); nb(s, 0x00); nbe16(s, r); nb(s, argc); }
static void n_cpdownsp(Ncs *s, int32_t o, int16_t sz) { nb(s, 0x01); nb(s, 0x01); nbe32(s, (uint32_t)o); nbe16(s, (uint16_t)sz); }
static void n_cptopsp(Ncs *s, int32_t o, int16_t sz) { nb(s, 0x03); nb(s, 0x01); nbe32(s, (uint32_t)o); nbe16(s, (uint16_t)sz); }
static void n_movsp(Ncs *s, int32_t o) { nb(s, 0x1B); nb(s, 0x00); nbe32(s, (uint32_t)o); }
static void n_mulff(Ncs *s) { nb(s, 0x16); nb(s, 0x21); }
static void n_retn(Ncs *s) { nb(s, 0x20); nb(s, 0x00); }
static void n_jsr(Ncs *s, int32_t o) { nb(s, 0x1E); nb(s, 0x00); nbe32(s, (uint32_t)o); }
static void n_rsaddi(Ncs *s) { nb(s, 0x02); nb(s, 0x03); }
/* header written at the end: "NCS V1.0" 'B' size(BE) */
static void n_begin(Ncs *s) { s->n = 0; s->over = 0; for (int i = 0; i < 13; i++) nb(s, 0); }
static void n_finish(Ncs *s) {
    if (s->over) { logline("net: NCS script too long, refused"); s->n = 0; return; }
    const char *h = "NCS V1.0B"; for (int i = 0; i < 9; i++) s->b[i] = (uint8_t)h[i];
    s->b[9] = s->n >> 24; s->b[10] = s->n >> 16; s->b[11] = s->n >> 8; s->b[12] = s->n;
}
/* routine numbers: Jade Empire nwscript.nss declaration order */
enum { R_CLEARALLACTIONS = 9, R_EXECUTESCRIPT = 8, R_ACTIONMOVETOLOCATION = 21, R_GETAREA = 24, R_GETPOSITION = 27, R_GETISOBJECTVALID = 42,
       R_GETISDEAD = 140, R_LOCATION = 215, R_APPLYEFFECT = 220, R_FLOATTOINT = 231, R_CREATEOBJECT = 243,
       R_JUMPTOLOCATION = 313, R_GETPLAYER = 548, R_GETLOCALOBJECT = 54, R_SETLOCALOBJECT = 58,
       R_EFFECTRESURRECTION = 82, R_SETOBJECTACTIVE = 756, R_SETCOMMANDABLE = 162,
       R_SETFACING = 10, R_GETFACING = 28, R_GETAPPEARANCE = 109, R_SETAPPEARANCE = 118, R_GETHEADTYPE = 119, R_SETFACINGPOINT = 143 };
#define NET_SLOT 192   /* local object slot on the player that holds the puppet */
static void n_push_puppet(Ncs *s) { n_consti(s, NET_SLOT); n_action(s, R_GETPLAYER, 0); n_action(s, R_GETLOCALOBJECT, 2); }
static void n_void_main_begin(Ncs *s) { n_begin(s); n_jsr(s, 8); n_retn(s); }
static void n_int_main_begin(Ncs *s) { n_begin(s); n_rsaddi(s); n_jsr(s, 8); n_retn(s); }
static void n_int_main_end(Ncs *s, int extra) { n_cpdownsp(s, -(extra + 4), 4); n_movsp(s, -extra); n_retn(s); n_finish(s); }
/* object on top: push component i of its position * 1000 as int; leaves 16 bytes */
static void n_pos_component(Ncs *s, int i) {
    n_action(s, R_GETPOSITION, 1); n_cptopsp(s, -12 + 4 * i, 4); n_constf(s, 1000.0f); n_mulff(s); n_action(s, R_FLOATTOINT, 1);
}
/* Location(GetArea(GetPlayer()), [x,y,z], 0) on top */
static void n_loc(Ncs *s, float x, float y, float z) {
    n_constf(s, 0.0f); n_constf(s, x); n_constf(s, y); n_constf(s, z);
    n_action(s, R_GETPLAYER, 0); n_action(s, R_GETAREA, 1); n_action(s, R_LOCATION, 3);
}

#include "steamnet.inc"

/* ---- state ---- */
static int g_netOn;
static int g_useSteam;
static SOCKET g_udp = INVALID_SOCKET;
static SOCKADDR_IN4 g_peer;
static char g_tpl[24];
static uint32_t g_seq, g_sent, g_recv, g_bad, g_lastSendTick, g_lastRecvTick, g_moves, g_jumps, g_spawns;
static volatile int32_t g_rxm, g_rym, g_rzm; static int g_rvalid, g_haveRemote;
#define g_rx (g_rxm / 1000.0f)
#define g_ry (g_rym / 1000.0f)
#define g_rz (g_rzm / 1000.0f)
static float g_ordX, g_ordY; static int g_haveOrder;
static int g_puppetState; /* 0 none, 1 alive */
static int g_netErr;
static volatile int32_t g_lastSent[3];
static uint32_t g_stuckSince, g_stuckJumps; static float g_bestD;
/* 0.7.2 follow tuning. g_tune bits (je.netTune, for A/B tests): 1 = old cadence (one script per frame),
   2 = old follow (walk until 4 m behind, no lead), 8 = 0.7.2 run rule (stays in run) */
static int g_tune;
static float g_rvx, g_rvy;            /* remote player velocity, m/s, from received positions */
static int g_ordRun; static uint32_t g_runOrders, g_walkOrders;
/* 0.8.0: look. Packet JEC2 also carries facing (centidegrees), appearance and head type. */
static int32_t g_myArea, g_rarea; static uint32_t g_areaHides, g_areaReads;
static int16_t g_myFc, g_myFs; static int32_t g_myApp = -1, g_myHead = -1;
static volatile int16_t g_rfc, g_rfs; static volatile int g_rfaceValid, g_rapp = -1, g_rhead = -1;
static int g_appliedApp = -999, g_appliedHead = -999; static int32_t g_faceSet = -1; static int16_t g_setFc, g_setFs; static uint32_t g_faceSetTick, g_faceSets, g_appSets;
static int g_netPaused;
static int g_netArmed;  /* nothing runs until je.netStart (scripts during early start-up crashed v0.6) */
static int g_crumbs;    /* first-run breadcrumbs written to jebridge.log */
#define CRUMB(bit, text) do { if (!(g_crumbs & (bit))) { g_crumbs |= (bit); logline(text); } } while (0) /* je.netPause: stop sending and stop driving the puppet (test mutation) */

/* ---- helpers on top of the in-memory script table ---- */
/* Store a script; a name keeps its own region, rewritten in place when the new code fits
   (the VM copies code into its own slot at load time, so rewriting is safe). */
static int ms_store(const char *name, const uint8_t *data, uint32_t n) {
    if (n < 13) return -3;                     /* refused script (n_finish), nothing to store */
    MemScript *m = ms_find(name);
    if (m && n <= m->cap) { memcpy(g_pool + m->off, data, n); m->len = n; return 0; }
    uint32_t cap = n < 512 ? 512 : n;
    if (g_poolUsed + cap > MS_POOL) return -2;
    if (!m) { if (g_msn >= MS_MAX) return -1; m = &g_ms[g_msn++]; s_cpy(m->name, name); }
    memcpy(g_pool + g_poolUsed, data, n); m->off = g_poolUsed; m->len = n; m->cap = cap; g_poolUsed += cap; return 0;
}
static uint32_t g_vmBusySkips;
static int vm_run(const char *name, uint32_t oid) {
    g_stage = name;
    int *app = *G_APP; if (!app) return -1;
    void *vm = *(void **)((char *)app + APP_VM); if (!vm) return -1;
    /* Only from an idle VM: PeekMessageA can be reached while a script is running (nested
       message pumping); RunScript treats depth -1 at vm+0x0C as idle. */
    if (*(int *)((char *)vm + 0xC) != -1) { g_vmBusySkips++; return -2; }
    CExoString s; s.p = 0; s.n = 0; ((ctor_t)F_EXOSTR_CTOR)(&s, name);
    int rc = ((run_t)F_RUNSCRIPT)(vm, &s, oid, 1); ((dtor_t)F_EXOSTR_DTOR)(&s); return rc;
}
/* Is there a player creature? NWScript GetPlayer (0x00487310) does app+0xA0 -> FUN_00424700 (thiscall,
   a lookup) and reads [result+8] WITHOUT a null check: after Esc > Quit game the player is gone and
   every net script crashed the game there (AV at 0x00487327, "Quit game" closed the game instead of
   going to the main menu). Same lookup here, with the check. */
typedef void *(__attribute__((thiscall)) *getpc_t)(void *);
static uint32_t g_noPlayerSkips;
static int player_exists(void) {
    int *app = *G_APP; if (!app) return 0;
    void *srv = *(void **)((char *)app + 0xA0); if (!srv) return 0;
    return ((getpc_t)0x00424700)(srv) != 0;
}
/* 0.8.1: the player's real orientation. GetFacing(GetPlayer()) stays constant for the player; the unit
   vector at creature+0x50 (creature = FUN_00424700 on app+0xA0, followed by the position at +0x5C)
   follows the player's turns (owner turned 128 -> 33 -> -127 deg; the last step's direction from the
   positions was -127 deg). Sent as cos/sin * 10000; 0/0 = unknown. */
static void player_dir(int16_t *c, int16_t *s) {
    *c = *s = 0;
    int *app = *G_APP; if (!app) return; void *srv = *(void **)((char *)app + 0xA0); if (!srv) return;
    uintptr_t pc = (uintptr_t)((getpc_t)0x00424700)(srv); if (!pc || !mem_readable(pc + 0x50, 8)) return;
    float x = *(float *)(pc + 0x50), y = *(float *)(pc + 0x54), l = x * x + y * y;
    if (l < 0.9f || l > 1.1f) return;
    *c = (int16_t)(x * 10000.0f); *s = (int16_t)(y * 10000.0f); if (!*c && !*s) *c = 1;
}
static __attribute__((noinline)) int vm_eval(const char *name, int *out) {
    int *app = *G_APP; if (!app) return 0;
    void *vm = *(void **)((char *)app + APP_VM); if (!vm) return 0;
    if (vm_run(name, 0x7F000000) != 1) { g_stage = "net idle"; return 0; }
    g_stage = "GetRunScriptReturnValue";
    int typ = 0, val = 0; if (!((ret_t)F_RETVAL)(vm, &typ, &val) || typ != 3) { g_stage = "net idle"; return 0; }
    g_stage = "net idle";
    *out = val; return 1;
}

static void net_build_static_scripts(void) {
    g_stage = "build static scripts";
    Ncs s;
    for (int i = 0; i < 3; i++) {            /* jbn_p0..2: player position component *1000 */
        n_int_main_begin(&s); n_action(&s, R_GETPLAYER, 0); n_pos_component(&s, i); n_int_main_end(&s, 16);
        char nm[8] = "jbn_p0"; nm[5] = '0' + i; ms_store(nm, s.b, s.n);
        n_int_main_begin(&s); n_push_puppet(&s); n_pos_component(&s, i); n_int_main_end(&s, 16);
        char nq[8] = "jbn_q0"; nq[5] = '0' + i; ms_store(nq, s.b, s.n);   /* puppet position */
    }
    n_int_main_begin(&s); n_action(&s, R_GETPLAYER, 0); n_action(&s, R_GETISOBJECTVALID, 1); n_int_main_end(&s, 4); ms_store("jbn_pcok", s.b, s.n);
    /* jbn_area (0.9.1): hash of the area's tag, h = h*31 + FindSubString(ALPHA, tag[i]) + 2 over 16
       lower-case chars (same tag -> same number in both games; scripts cannot return strings). */
    {
        static const char *ALPHA = "abcdefghijklmnopqrstuvwxyz0123456789_";
        n_int_main_begin(&s); n_consti(&s, 0);
        n_action(&s, R_GETPLAYER, 0); n_action(&s, 24 /* GetArea */, 1); n_action(&s, 168 /* GetTag */, 1); n_action(&s, 61 /* GetStringLowerCase */, 1);
        for (int i = 0; i < 16; i++) {
            n_consti(&s, 1); n_consti(&s, i); n_cptopsp(&s, -12, 4); n_action(&s, 65 /* GetSubString */, 3);
            n_consts(&s, ALPHA); n_action(&s, 66 /* FindSubString */, 2); n_consti(&s, 2); nb(&s, 0x14); nb(&s, 0x20);   /* ADDII */
            n_cptopsp(&s, -12, 4); n_consti(&s, 31); nb(&s, 0x16); nb(&s, 0x20); nb(&s, 0x14); nb(&s, 0x20);              /* MULII, ADDII */
            n_cpdownsp(&s, -12, 4); n_movsp(&s, -4);
        }
        n_cptopsp(&s, -8, 4); n_int_main_end(&s, 12); ms_store("jbn_area", s.b, s.n);
    }
    /* jbn_pa / jbn_ph: GetAppearance / GetHeadType(player) */
    n_int_main_begin(&s); n_action(&s, R_GETPLAYER, 0); n_action(&s, R_GETAPPEARANCE, 1); n_int_main_end(&s, 4); ms_store("jbn_pa", s.b, s.n);
    n_int_main_begin(&s); n_action(&s, R_GETPLAYER, 0); n_action(&s, R_GETHEADTYPE, 1); n_int_main_end(&s, 4); ms_store("jbn_ph", s.b, s.n);
    n_int_main_begin(&s); n_push_puppet(&s); n_action(&s, R_GETISDEAD, 1); n_int_main_end(&s, 4); ms_store("jbn_qdead", s.b, s.n);
    /* resurrect puppet: ApplyEffectToObject(puppet, INSTANT, EffectResurrection(TRUE), 0.0).
       0.8.0: FALSE revived it in Two Rivers only; in a later area only TRUE works (GetIsDead stayed 1). */
    n_void_main_begin(&s); n_constf(&s, 0.0f); n_consti(&s, 1); n_action(&s, R_EFFECTRESURRECTION, 1); n_consti(&s, 0);
    n_push_puppet(&s); n_action(&s, R_APPLYEFFECT, 4); n_retn(&s); n_finish(&s); ms_store("jbn_res", s.b, s.n);
    /* run jbn_act as the puppet */
    n_void_main_begin(&s); n_consts(&s, "jbn_act"); n_push_puppet(&s); n_action(&s, R_EXECUTESCRIPT, 2); n_retn(&s); n_finish(&s); ms_store("jbn_go", s.b, s.n);
    /* deactivate puppet */
    n_void_main_begin(&s); for (int i = 0; i < 4; i++) n_consti(&s, -1); n_consti(&s, 0); n_push_puppet(&s); n_action(&s, R_SETOBJECTACTIVE, 6);
    n_retn(&s); n_finish(&s); ms_store("jbn_off", s.b, s.n);
}
static int net_spawn_puppet(float x, float y, float z) {
    Ncs s; n_void_main_begin(&s);
    n_loc(&s, x, y, z); n_consts(&s, g_tpl); n_consti(&s, 2 /* OBJECT_TYPE_CREATURE */); n_action(&s, R_CREATEOBJECT, 3);
    n_cptopsp(&s, -4, 4); n_consti(&s, NET_SLOT); n_action(&s, R_GETPLAYER, 0); n_action(&s, R_SETLOCALOBJECT, 3); n_movsp(&s, -4);
    n_retn(&s); n_finish(&s);
    if (ms_store("jbn_spawn", s.b, s.n)) return 0;
    if (vm_run("jbn_spawn", 0x7F000000) != 1) return 0;
    vm_run("jbn_res", 0x7F000000);
    int dead = 1; vm_eval("jbn_qdead", &dead);
    return dead == 0;
}
static void net_order(float x, float y, float z, int run, int jump) {
    g_faceSet = -1;
    Ncs s; n_void_main_begin(&s);
    /* 0.7.2: ActionMoveToLocation is queued behind earlier orders; without clearing, the puppet walked
       every stale waypoint in turn (stop, start) and trailed by seconds. tune bit 4 = keep old queueing. */
    if (!jump && !(g_tune & 6)) n_action(&s, R_CLEARALLACTIONS, 0);
    /* The remote player's puppet must always obey: a jump also restores commandability
       (SetCommandable(TRUE, OBJECT_SELF): push OBJECT_SELF, then TRUE). */
    if (jump) { n_consti(&s, 0); s.b[s.n - 6 + 1] = 0x06; n_consti(&s, 1); n_action(&s, R_SETCOMMANDABLE, 2);
                n_consti(&s, 1); n_constf(&s, 0.0f); n_constf(&s, x); n_constf(&s, y); n_constf(&s, z);
                n_consti(&s, 0); /* OBJECT_SELF */ s.b[s.n - 6 + 1] = 0x06; /* CONSTO */
                n_action(&s, R_GETAREA, 1); n_action(&s, R_LOCATION, 3); n_action(&s, R_JUMPTOLOCATION, 2); }
    else      { n_consti(&s, -1); n_consti(&s, run); n_constf(&s, 0.0f); n_constf(&s, x); n_constf(&s, y); n_constf(&s, z);
                n_consti(&s, 0); s.b[s.n - 6 + 1] = 0x06;
                n_action(&s, R_GETAREA, 1); n_action(&s, R_LOCATION, 3); n_action(&s, R_ACTIONMOVETOLOCATION, 3); }
    n_retn(&s); n_finish(&s);
    if (ms_store("jbn_act", s.b, s.n)) return;
    vm_run("jbn_go", 0x7F000000);
    if (jump) g_jumps++; else g_moves++;
}

/* 0.8.0: give the puppet the remote player's look: SetAppearance(puppet, app, head). A player head of
   -1 ("built-in") must be passed as 0: -1 keeps the template's head (the owner saw the abbot's head). */
static void net_apply_appearance(void) {
    int head = g_rhead < 0 ? 0 : g_rhead;
    Ncs s; n_void_main_begin(&s); n_consti(&s, head); n_consti(&s, g_rapp); n_push_puppet(&s); n_action(&s, R_SETAPPEARANCE, 3);
    n_retn(&s); n_finish(&s);
    if (ms_store("jbn_app", s.b, s.n)) return;
    if (vm_run("jbn_app", 0x7F000000) == 1) { g_appliedApp = g_rapp; g_appliedHead = g_rhead; g_appSets++; }
}
/* SetFacingPoint(vTarget = puppet + 2 m along the remote direction, nAnimate = 0, oTarget = puppet, FACING_TURNSHORTEST) */
static void net_set_facing(float px, float py, float pz) {
    float dx = g_rfc / 10000.0f, dy = g_rfs / 10000.0f;
    Ncs s; n_void_main_begin(&s); n_consti(&s, 0); n_push_puppet(&s); n_consti(&s, 0);
    n_constf(&s, px + 2.0f * dx); n_constf(&s, py + 2.0f * dy); n_constf(&s, pz); n_action(&s, R_SETFACINGPOINT, 4);
    n_retn(&s); n_finish(&s);
    if (ms_store("jbn_face", s.b, s.n)) return;
    if (vm_run("jbn_face", 0x7F000000) == 1) { g_faceSet = 1; g_setFc = g_rfc; g_setFs = g_rfs; g_faceSets++; }
}
/* cos of the angle between the last applied and the current remote direction, *1e8 */
static int32_t facedot(void) { return (int32_t)g_setFc * g_rfc + (int32_t)g_setFs * g_rfs; }

static void cb_on_damage(const uint8_t *p, int n);
#include "savexfer.inc"
#include "combat.inc"

static int net_parse_config(void) {
    char p[300], buf[512]; s_cpy(p, g_dir); s_cat(p, "bridge\\net.txt");
    HANDLE h = CreateFileA(p, 0x80000000, 1, 0, 3, 0x80, 0); if (h == (HANDLE)-1) return 0;
    DWORD n = 0; ReadFile(h, buf, sizeof buf - 1, &n, 0); CloseHandle(h); buf[n] = 0;
    uint16_t lport = 0, pport = 0; uint32_t ip = 0; s_cpy(g_tpl, "j00h_abbot0");
    for (char *line = buf; *line; ) {
        char *e = line; while (*e && *e != '\n' && *e != '\r') e++; char save = *e; *e = 0;
        if (line[0] == 'l' && s_str(line, "local ") == line) lport = (uint16_t)s2u(line + 6);
        else if (s_str(line, "peer ") == line) {
            const char *q = line + 5; uint32_t parts[4] = {0}; for (int i = 0; i < 4; i++) { parts[i] = s2u(q); while (*q >= '0' && *q <= '9') q++; if (*q == '.') q++; }
            while (*q == ' ') q++; pport = (uint16_t)s2u(q);
            ip = parts[0] | (parts[1] << 8) | (parts[2] << 16) | (parts[3] << 24);
        } else if (s_str(line, "transport steam") == line) { g_useSteam = 1;
        } else if (s_str(line, "role host") == line) { g_steamHost = 1;
        } else if (s_str(line, "role join") == line) { g_steamHost = 0;
        } else if (s_str(line, "lobby ") == line) { g_wantLobby = s2u64(line + 6);
        } else if (s_str(line, "getsave 1") == line) { g_getSave = 1;
        } else if (s_str(line, "template ") == line) { int i = 0; const char *q = line + 9; while (*q && *q != ' ' && i < 16) g_tpl[i++] = *q++; g_tpl[i] = 0; }
        *e = save; line = *e ? e + 1 : e;
    }
    if (g_useSteam) { logline(g_steamHost ? "jebridge: net on: transport steam, role host" : "jebridge: net on: transport steam, role join"); return 1; }
    if (!lport || !pport || !ip) { logline("jebridge: net.txt incomplete (need local, peer)"); return 0; }
    g_udp = socket(2, 2 /*SOCK_DGRAM*/, 17); if (g_udp == INVALID_SOCKET) return 0;
    SOCKADDR_IN4 sa; memset(&sa, 0, sizeof sa); sa.sin_family = 2;
    sa.addr = ((ip & 0xFF) == 127) ? 0x0100007F : 0;   /* loopback peer: bind loopback only (no firewall prompt) */
    sa.sin_port = (unsigned short)((lport >> 8) | (lport << 8));
    if (bind(g_udp, &sa, sizeof sa) != 0) { logline("jebridge: net: bind failed"); closesocket(g_udp); g_udp = INVALID_SOCKET; return 0; }
    unsigned long nbio = 1; ioctlsocket(g_udp, (long)0x8004667E /*FIONBIO*/, &nbio);
    memset(&g_peer, 0, sizeof g_peer); g_peer.sin_family = 2; g_peer.addr = ip; g_peer.sin_port = (unsigned short)((pport >> 8) | (pport << 8));
    char msg[120], a[16]; s_cpy(msg, "jebridge: net on: local udp "); u2s(a, lport); s_cat(msg, a); s_cat(msg, " -> peer port "); u2s(a, pport); s_cat(msg, a);
    s_cat(msg, ", template "); s_cat(msg, g_tpl); logline(msg);
    return 1;
}

static float fisqrt(float v) { /* 1/sqrt(v), no CRT */ float lo = 0, hi = 1e3f, s = 0; for (int i = 0; i < 40; i++) { s = (lo + hi) / 2; if (s * s * v > 1.0f) hi = s; else lo = s; } return s; }
static float fdist2(float ax, float ay, float bx, float by) { float dx = ax - bx, dy = ay - by; return dx * dx + dy * dy; }

/* main thread, every frame.
   Runs at most ONE script per frame: in v0.6, evaluating several scripts back to back in one frame
   returned a wrong value (y = -1) although each script alone was right. Steps:
   0 pcok, 1 x, 2 y, 3 z  -> send; then (if a remote player is known) 4 puppet x, 5 puppet y, 6 puppet z -> drive. */
static volatile int g_pv[7];
static int g_step;
static void net_tick(void) {
    /* Players without the bridge client arm net with Ctrl+Shift+N (game window focused, after the
       save is loaded). One beep = armed. */
    if (g_netOn && !g_netArmed && *G_APPACTIVE &&
        (GetAsyncKeyState(0x11) & 0x8000) && (GetAsyncKeyState(0x10) & 0x8000) && (GetAsyncKeyState('N') & 0x8000)) {
        g_netArmed = 1; logline("net: armed by Ctrl+Shift+N"); MessageBeep(0);
    }
    if (!g_netOn || !g_netArmed) return;
    CRUMB(1, "net: first armed tick");
    static int built; if (!built) { net_build_static_scripts(); built = 1; CRUMB(2, "net: static scripts built"); }
    if (g_useSteam) {
        static int started; if (!started) { started = 1; g_stage = "steam start"; if (!steam_start()) { g_netOn = 0; g_stage = "net idle"; return; } }
        g_stage = "steam pump"; steam_pump();
    }
    uint32_t now = GetTickCount();
    g_stage = "recv";
    for (int k = 0; k < 64; k++) {
        char pkt[512]; int r;
        if (g_useSteam) r = steam_recv(pkt, sizeof pkt);
        else { SOCKADDR_IN4 from; int fl = sizeof from; r = recvfrom(g_udp, pkt, sizeof pkt, 0, &from, &fl); }
        if (r <= 0) break;
        if (r >= 8 && pkt[0] == 'J' && pkt[1] == 'E' && pkt[2] == 'F' && pkt[3] == '1') { cb_on_snapshot(pkt, r); continue; }   /* 0.10.0 */
        if (r < 24 || pkt[0] != 'J' || pkt[1] != 'E' || pkt[2] != 'C' || !((pkt[3] == '1' && r == 24) || (pkt[3] == '2' && r == 32) || (pkt[3] == '3' && r == 36))) { g_bad++; continue; }
        if (pkt[3] == '3') { int32_t ar; memcpy(&ar, pkt + 32, 4); g_rarea = ar; } else g_rarea = 0;
        if (pkt[3] >= '2') { int16_t fc, fs, a, h; memcpy(&fc, pkt + 24, 2); memcpy(&fs, pkt + 26, 2); memcpy(&a, pkt + 28, 2); memcpy(&h, pkt + 30, 2);
                             g_rfc = fc; g_rfs = fs; g_rfaceValid = fc || fs; g_rapp = a; g_rhead = h; }
        { int32_t v; memcpy(&v, pkt + 8, 4); g_rxm = v; memcpy(&v, pkt + 12, 4); g_rym = v; memcpy(&v, pkt + 16, 4); g_rzm = v; }
        uint32_t flags; memcpy(&flags, pkt + 20, 4); g_rvalid = flags & 1; g_haveRemote = 1; g_recv++; g_lastRecvTick = now;
        { /* 0.7.3: speed over the last ~0.5 s from a ring of received positions. Short windows
             (150 ms) flickered with bunched packets and the puppet kept switching walk/run. */
          static int32_t hx[16], hy[16]; static uint32_t ht[16]; static int hn, hi;
          hx[hi] = g_rxm; hy[hi] = g_rym; ht[hi] = now; int cur = hi; hi = (hi + 1) & 15; if (hn < 16) hn++;
          int best = -1;
          for (int k = 1; k < hn; k++) { int j = (cur - k) & 15; uint32_t age = now - ht[j]; if (age > 900) break; best = j; if (age >= 500) break; }
          if (best >= 0 && now - ht[best] >= 250) { float dt = (now - ht[best]) / 1000.0f;
              g_rvx = (g_rxm - hx[best]) / 1000.0f / dt; g_rvy = (g_rym - hy[best]) / 1000.0f / dt; }
          else if (best < 0) g_rvx = g_rvy = 0; }
        CRUMB(128, "net: first packet received");
    }
    g_stage = "save xfer"; xf_tick();      /* 0.9.0: works without a loaded game (guest at the main menu) */
    g_stage = "net idle";
    if (g_netPaused) return;
    g_stage = "player check";
    if (!player_exists()) { g_noPlayerSkips++; g_step = 0; g_puppetState = 0; g_haveOrder = 0; g_myArea = 0; g_stage = "net idle"; return; }
    g_stage = "combat"; cb_tick();
    g_stage = "net idle";
    if (now - g_lastRecvTick > 600) g_rvx = g_rvy = 0;        /* no fresh data: do not extrapolate */
    if (g_step == 0) { if (now - g_lastSendTick < 50) return; g_lastSendTick = now; }
    int split = g_tune & 1;   /* 0.7.2: player x,y,z in one frame, puppet x,y,z in the next (was 7 frames) */
    static const char *const names[7] = { "jbn_pcok", "jbn_p0", "jbn_p1", "jbn_p2", "jbn_q0", "jbn_q1", "jbn_q2" };
    if (g_step <= 3) {
        CRUMB(4, "net: first player eval");
        while (g_step <= 3) {
            if (!vm_eval(names[g_step], (int *)&g_pv[g_step]) || (g_step == 0 && !g_pv[0])) { g_step = 0; return; }
            if (++g_step <= 3 && split) return;
        }
        /* step 3 done: facing (and now and then appearance/head), then send */
        player_dir(&g_myFc, &g_myFs);
        { if (!g_myArea || (g_seq % 20) == 0) { int h = 0; if (vm_eval("jbn_area", &h)) { g_myArea = h; g_areaReads++; } } }
        if (g_myApp < 0 || (g_seq % 40) == 0) { int a = -1, h = -1; if (vm_eval("jbn_pa", &a)) g_myApp = a; if (vm_eval("jbn_ph", &h)) g_myHead = h; }
        CRUMB(8, "net: first player position read");
        char pkt[36]; uint32_t fl = 1; int32_t v;
        pkt[0] = 'J'; pkt[1] = 'E'; pkt[2] = 'C'; pkt[3] = '1'; g_seq++; memcpy(pkt + 4, (const void *)&g_seq, 4);
        v = g_pv[1]; memcpy(pkt + 8, &v, 4); v = g_pv[2]; memcpy(pkt + 12, &v, 4); v = g_pv[3]; memcpy(pkt + 16, &v, 4); memcpy(pkt + 20, &fl, 4);
        pkt[3] = '3'; memcpy(pkt + 32, &g_myArea, 4); memcpy(pkt + 24, &g_myFc, 2); memcpy(pkt + 26, &g_myFs, 2); { int16_t a = (int16_t)g_myApp, h = (int16_t)g_myHead; memcpy(pkt + 28, &a, 2); memcpy(pkt + 30, &h, 2); }
        g_lastSent[0] = g_pv[1]; g_lastSent[1] = g_pv[2]; g_lastSent[2] = g_pv[3];
        g_stage = "sendto";
        if (g_useSteam) { uint32_t before = g_sSent, eb = g_sSendErr; steam_send(pkt, 36); if (g_sSent != before) g_sent++; else if (g_sSendErr != eb) g_netErr++; }
        else if (sendto(g_udp, pkt, 36, 0, &g_peer, sizeof g_peer) == 36) g_sent++; else g_netErr++;
        g_stage = "net idle";
        CRUMB(16, "net: first packet sent");
        if (!g_haveRemote || !g_rvalid || now - g_lastRecvTick > 3000) { g_step = 0; return; }
        /* 0.9.1: other area (or ours not read yet): no puppet. One that exists is switched off; a new one
           is spawned beside the remote player once both are in the same area. */
        if (!g_myArea || (g_rarea && g_rarea != g_myArea)) {
            if (g_puppetState == 1) { vm_run("jbn_off", 0x7F000000); g_puppetState = 0; g_areaHides++; }
            g_step = 0; return;
        }
        if (g_puppetState == 0) {
            CRUMB(32, "net: first puppet spawn attempt");
            if (net_spawn_puppet(g_rx + 1.0f, g_ry, g_rz)) { g_puppetState = 1; g_spawns++; g_haveOrder = 0; g_appliedApp = g_appliedHead = -999; g_faceSet = -1; }
            g_step = 0; return;
        }
        return;                                   /* puppet position from the next frame on */
    }
    if (g_step <= 6) {
        while (g_step <= 6) {
            if (!vm_eval(names[g_step], (int *)&g_pv[g_step])) { g_puppetState = 0; g_step = 0; return; }
            if (++g_step <= 6 && split) return;
        }
    }
    g_step = 0;
    CRUMB(64, "net: first puppet drive");
    float fx = g_pv[4] / 1000.0f, fy = g_pv[5] / 1000.0f;
    /* tune 32: no look (appearance/facing) for A/B tests */
    if (!(g_tune & 32) && g_rapp > 0 && (g_rapp != g_appliedApp || g_rhead != g_appliedHead)) { net_apply_appearance(); return; }
    int old = g_tune & 2;
    float sp2 = g_rvx * g_rvx + g_rvy * g_rvy;
    /* Lead: aim where the remote player will be in ~0.3 s (network + order delay), at most 2 m ahead. */
    float lx = 0, ly = 0;
    if (!old && sp2 > 1.0f) { lx = g_rvx * 0.5f; ly = g_rvy * 0.5f; float l2 = lx * lx + ly * ly; if (l2 > 9.0f) { float k = 3.0f * fisqrt(l2); lx *= k; ly *= k; } }
    float tx = g_rx + lx, ty = g_ry + ly;
    float gx = tx - 1.0f, gy = ty;                         /* stand 1 m beside the remote player */
    float d2 = fdist2(fx, fy, tx, ty);
    /* Run when the remote player runs (> 2.5 m/s) or when more than 2.5 m behind (was: only > 4 m). */
    /* 0.7.3: copy the remote player's gait. Measured: walk 1.5 m/s, jog 3.1, run 5.3. Walk below
       ~2.3 m/s, run above; run anyway to catch up when > 4 m behind. Hysteresis (2.0 / 2.6 m/s, 3 / 4 m)
       because a flickering estimate made the puppet restart its order (0.7.2 stayed in run while the
       remote player walked: it only went back to walking below 1 m/s). */
    int run;
    if (old) run = d2 > 16.0f;
    else if (g_tune & 8)  /* 0.7.2 rule, kept for the gait test's mutation run */
        run = g_ordRun ? !(sp2 < 1.0f && d2 < 1.8f * 1.8f) : (sp2 > 2.0f * 2.0f || d2 > 2.5f * 2.5f);
    else if (g_ordRun) run = sp2 > 2.0f * 2.0f || d2 > 3.0f * 3.0f;
    else               run = sp2 > 2.6f * 2.6f || d2 > 4.0f * 4.0f;
    if (d2 > 15.0f * 15.0f) { net_order(gx, gy, g_rz, 0, 1); g_ordX = gx; g_ordY = gy; g_haveOrder = 1; return; }
    /* 0.8.1: standing facing. Direction from the player's memory (0.8.0's GetFacing value was stale).
       Applied when the remote player stands, the puppet itself stands (< 5 cm since the last drive; it
       may have stopped a bit more than 1.6 m away) and the direction differs by > ~10 deg (cos 0.985). */
    { static float lpx, lpy; int still = fdist2(fx, fy, lpx, lpy) < 0.05f * 0.05f; lpx = fx; lpy = fy;
      if (!(g_tune & 32) && g_rfaceValid && still && sp2 < 0.3f * 0.3f && now - g_faceSetTick > 250 &&
          (g_faceSet < 0 || facedot() < 98500000)) { g_faceSetTick = now; net_set_facing(fx, fy, g_pv[6] / 1000.0f); return; } }
    if (d2 <= 1.6f * 1.6f && (old || sp2 < 1.0f)) { g_stuckSince = 0; return; }
    /* 0.8.1: remote player standing and the puppet within 2 m: no new walk order (a run->walk change or
       a small target shift made it step and turn away right after it had been turned). */
    if (!old && sp2 < 0.3f * 0.3f && d2 <= 2.0f * 2.0f) { g_stuckSince = 0; return; }
    /* Stuck: more than 3 m away and the distance has not shrunk by 0.3 m for 2 s (e.g. pathing
       blocked by fences): jump next to the remote player instead of walking. */
    if (d2 > 3.0f * 3.0f) {
        float d = 0; { float lo = 0, hi = 20; for (int it = 0; it < 24; it++) { float m = (lo + hi) / 2; if (m * m < d2) lo = m; else hi = m; } d = lo; }
        /* 0.7.4: a puppet that is moving is not stuck, even when the gap grows (a remote player
           running faster than the puppet triggered ~1 jump per 3 s in a real session). */
        static float ax, ay;
        int moving = !old && fdist2(fx, fy, ax, ay) > 0.5f * 0.5f;
        if (!g_stuckSince || d < g_bestD - 0.3f || moving) { g_stuckSince = now; g_bestD = d; ax = fx; ay = fy; }
        else if (now - g_stuckSince > 2000) {
            net_order(gx, gy, g_rz, 0, 1); g_ordX = gx; g_ordY = gy; g_haveOrder = 1; g_stuckJumps++; g_stuckSince = 0; return;
        }
    } else g_stuckSince = 0;
    /* New orders: old mode on every 0.5 m target change; new mode when the target moved 1.5 m, the run
       flag changed, or the puppet already reached the last order point (fewer restarts). */
    int reorder = old ? fdist2(gx, gy, g_ordX, g_ordY) > 0.5f * 0.5f
                      : (fdist2(gx, gy, g_ordX, g_ordY) > 1.5f * 1.5f || run != g_ordRun ||
                         (fdist2(fx, fy, g_ordX, g_ordY) < 1.0f && fdist2(gx, gy, g_ordX, g_ordY) > 0.3f * 0.3f));
    if (!g_haveOrder || reorder) {
        net_order(gx, gy, g_rz, run, 0); g_ordX = gx; g_ordY = gy; g_haveOrder = 1; g_ordRun = run;
        if (run) g_runOrders++; else g_walkOrders++;
    }
}

/* je.netStatus */
static void net_status(char *out) {
    char a[24];
    s_cpy(out, "{\"result\":{\"on\":"); s_cat(out, g_netOn ? "true" : "false"); s_cat(out, ",\"armed\":"); s_cat(out, g_netArmed ? "true" : "false"); s_cat(out, ",\"paused\":"); s_cat(out, g_netPaused ? "true" : "false");
    s_cat(out, ",\"sent\":"); u2s(a, g_sent); s_cat(out, a);
    s_cat(out, ",\"recv\":"); u2s(a, g_recv); s_cat(out, a);
    s_cat(out, ",\"bad\":"); u2s(a, g_bad); s_cat(out, a);
    s_cat(out, ",\"sendErr\":"); u2s(a, (uint32_t)g_netErr); s_cat(out, a);
    s_cat(out, ",\"puppet\":"); s_cat(out, g_puppetState ? "\"alive\"" : "\"none\"");
    s_cat(out, ",\"spawns\":"); u2s(a, g_spawns); s_cat(out, a);
    s_cat(out, ",\"moves\":"); u2s(a, g_moves); s_cat(out, a);
    s_cat(out, ",\"jumps\":"); u2s(a, g_jumps); s_cat(out, a);
    s_cat(out, ",\"lastSentMm\":["); i2s(a, g_lastSent[0]); s_cat(out, a); s_cat(out, ","); i2s(a, g_lastSent[1]); s_cat(out, a); s_cat(out, ","); i2s(a, g_lastSent[2]); s_cat(out, a); s_cat(out, "]");
    s_cat(out, ",\"tune\":"); u2s(a, (uint32_t)g_tune); s_cat(out, a);
    s_cat(out, ",\"remoteSpeedMms\":"); u2s(a, (uint32_t)(1000.0f / fisqrt(g_rvx * g_rvx + g_rvy * g_rvy + 1e-6f))); s_cat(out, a);
    s_cat(out, ",\"remoteDir\":["); i2s(a, g_rfc); s_cat(out, a); s_cat(out, ","); i2s(a, g_rfs); s_cat(out, a); s_cat(out, "]");
    s_cat(out, ",\"myDir\":["); i2s(a, g_myFc); s_cat(out, a); s_cat(out, ","); i2s(a, g_myFs); s_cat(out, a); s_cat(out, "]");
    s_cat(out, ",\"remoteApp\":"); i2s(a, g_rapp); s_cat(out, a);
    s_cat(out, ",\"remoteHead\":"); i2s(a, g_rhead); s_cat(out, a);
    s_cat(out, ",\"appSets\":"); u2s(a, g_appSets); s_cat(out, a);
    s_cat(out, ",\"faceSets\":"); u2s(a, g_faceSets); s_cat(out, a);
    s_cat(out, ",\"myArea\":"); i2s(a, g_myArea); s_cat(out, a);
    s_cat(out, ",\"remoteArea\":"); i2s(a, g_rarea); s_cat(out, a);
    s_cat(out, ",\"areaHides\":"); u2s(a, g_areaHides); s_cat(out, a);
    s_cat(out, ",\"noPlayerSkips\":"); u2s(a, g_noPlayerSkips); s_cat(out, a);
    s_cat(out, ",\"runOrders\":"); u2s(a, g_runOrders); s_cat(out, a);
    s_cat(out, ",\"walkOrders\":"); u2s(a, g_walkOrders); s_cat(out, a);
    s_cat(out, ",\"stuckJumps\":"); u2s(a, g_stuckJumps); s_cat(out, a);
    s_cat(out, ",\"vmBusySkips\":"); u2s(a, g_vmBusySkips); s_cat(out, a);
    s_cat(out, ",\"remoteMm\":["); i2s(a, g_rxm); s_cat(out, a); s_cat(out, ",");
    i2s(a, g_rym); s_cat(out, a); s_cat(out, ","); i2s(a, g_rzm); s_cat(out, a); s_cat(out, "]");
    s_cat(out, ",\"lastRecvAgoMs\":"); if (g_lastRecvTick) { u2s(a, GetTickCount() - g_lastRecvTick); s_cat(out, a); } else s_cat(out, "-1");
    if (g_useSteam) { steam_status(out); xf_status(out); cb_status(out); }
    s_cat(out, "}}");
}
