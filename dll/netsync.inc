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
typedef struct { uint8_t b[512]; uint32_t n; } Ncs;
static void nb(Ncs *s, uint8_t v) { if (s->n < sizeof s->b) s->b[s->n++] = v; }
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
static void n_begin(Ncs *s) { s->n = 0; for (int i = 0; i < 13; i++) nb(s, 0); }
static void n_finish(Ncs *s) {
    const char *h = "NCS V1.0B"; for (int i = 0; i < 9; i++) s->b[i] = (uint8_t)h[i];
    s->b[9] = s->n >> 24; s->b[10] = s->n >> 16; s->b[11] = s->n >> 8; s->b[12] = s->n;
}
/* routine numbers: Jade Empire nwscript.nss declaration order */
enum { R_EXECUTESCRIPT = 8, R_ACTIONMOVETOLOCATION = 21, R_GETAREA = 24, R_GETPOSITION = 27, R_GETISOBJECTVALID = 42,
       R_GETISDEAD = 140, R_LOCATION = 215, R_APPLYEFFECT = 220, R_FLOATTOINT = 231, R_CREATEOBJECT = 243,
       R_JUMPTOLOCATION = 313, R_GETPLAYER = 548, R_GETLOCALOBJECT = 54, R_SETLOCALOBJECT = 58,
       R_EFFECTRESURRECTION = 82, R_SETOBJECTACTIVE = 756, R_SETCOMMANDABLE = 162 };
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

/* ---- state ---- */
static int g_netOn;
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
static int g_netPaused;
static int g_netArmed;  /* nothing runs until je.netStart (scripts during early start-up crashed v0.6) */
static int g_crumbs;    /* first-run breadcrumbs written to jebridge.log */
#define CRUMB(bit, text) do { if (!(g_crumbs & (bit))) { g_crumbs |= (bit); logline(text); } } while (0) /* je.netPause: stop sending and stop driving the puppet (test mutation) */

/* ---- helpers on top of the in-memory script table ---- */
/* Store a script; a name keeps its own region, rewritten in place when the new code fits
   (the VM copies code into its own slot at load time, so rewriting is safe). */
static int ms_store(const char *name, const uint8_t *data, uint32_t n) {
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
    n_int_main_begin(&s); n_push_puppet(&s); n_action(&s, R_GETISDEAD, 1); n_int_main_end(&s, 4); ms_store("jbn_qdead", s.b, s.n);
    /* resurrect puppet: ApplyEffectToObject(puppet, INSTANT, EffectResurrection(0), 0.0) */
    n_void_main_begin(&s); n_constf(&s, 0.0f); n_consti(&s, 0); n_action(&s, R_EFFECTRESURRECTION, 1); n_consti(&s, 0);
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
    Ncs s; n_void_main_begin(&s);
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
        } else if (s_str(line, "template ") == line) { int i = 0; const char *q = line + 9; while (*q && *q != ' ' && i < 16) g_tpl[i++] = *q++; g_tpl[i] = 0; }
        *e = save; line = *e ? e + 1 : e;
    }
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

static float fdist2(float ax, float ay, float bx, float by) { float dx = ax - bx, dy = ay - by; return dx * dx + dy * dy; }

/* main thread, every frame.
   Runs at most ONE script per frame: in v0.6, evaluating several scripts back to back in one frame
   returned a wrong value (y = -1) although each script alone was right. Steps:
   0 pcok, 1 x, 2 y, 3 z  -> send; then (if a remote player is known) 4 puppet x, 5 puppet y, 6 puppet z -> drive. */
static volatile int g_pv[7];
static int g_step;
static void net_tick(void) {
    if (!g_netOn || !g_netArmed) return;
    CRUMB(1, "net: first armed tick");
    static int built; if (!built) { net_build_static_scripts(); built = 1; CRUMB(2, "net: static scripts built"); }
    uint32_t now = GetTickCount();
    g_stage = "recvfrom";
    for (int k = 0; k < 64; k++) {
        char pkt[64]; SOCKADDR_IN4 from; int fl = sizeof from;
        int r = recvfrom(g_udp, pkt, sizeof pkt, 0, &from, &fl);
        if (r <= 0) break;
        if (r != 24 || pkt[0] != 'J' || pkt[1] != 'E' || pkt[2] != 'C' || pkt[3] != '1') { g_bad++; continue; }
        { int32_t v; memcpy(&v, pkt + 8, 4); g_rxm = v; memcpy(&v, pkt + 12, 4); g_rym = v; memcpy(&v, pkt + 16, 4); g_rzm = v; }
        uint32_t flags; memcpy(&flags, pkt + 20, 4); g_rvalid = flags & 1; g_haveRemote = 1; g_recv++; g_lastRecvTick = now;
        CRUMB(128, "net: first packet received");
    }
    g_stage = "net idle";
    if (g_netPaused) return;
    if (g_step == 0) { if (now - g_lastSendTick < 66) return; g_lastSendTick = now; }
    static const char *const names[7] = { "jbn_pcok", "jbn_p0", "jbn_p1", "jbn_p2", "jbn_q0", "jbn_q1", "jbn_q2" };
    if (g_step <= 3) {
        CRUMB(4, "net: first player eval");
        if (!vm_eval(names[g_step], (int *)&g_pv[g_step]) || (g_step == 0 && !g_pv[0])) { g_step = 0; return; }
        if (++g_step <= 3) return;
        /* step 3 done: send */
        CRUMB(8, "net: first player position read");
        char pkt[24]; uint32_t fl = 1; int32_t v;
        pkt[0] = 'J'; pkt[1] = 'E'; pkt[2] = 'C'; pkt[3] = '1'; g_seq++; memcpy(pkt + 4, (const void *)&g_seq, 4);
        v = g_pv[1]; memcpy(pkt + 8, &v, 4); v = g_pv[2]; memcpy(pkt + 12, &v, 4); v = g_pv[3]; memcpy(pkt + 16, &v, 4); memcpy(pkt + 20, &fl, 4);
        g_lastSent[0] = g_pv[1]; g_lastSent[1] = g_pv[2]; g_lastSent[2] = g_pv[3];
        g_stage = "sendto";
        if (sendto(g_udp, pkt, 24, 0, &g_peer, sizeof g_peer) == 24) g_sent++; else g_netErr++;
        g_stage = "net idle";
        CRUMB(16, "net: first packet sent");
        if (!g_haveRemote || !g_rvalid || now - g_lastRecvTick > 3000) { g_step = 0; return; }
        if (g_puppetState == 0) {
            CRUMB(32, "net: first puppet spawn attempt");
            if (net_spawn_puppet(g_rx + 1.0f, g_ry, g_rz)) { g_puppetState = 1; g_spawns++; g_haveOrder = 0; }
            g_step = 0; return;
        }
        return;                                   /* puppet position from the next frame on */
    }
    if (g_step <= 6) {
        if (!vm_eval(names[g_step], (int *)&g_pv[g_step])) { g_puppetState = 0; g_step = 0; return; }
        if (++g_step <= 6) return;
    }
    g_step = 0;
    CRUMB(64, "net: first puppet drive");
    float fx = g_pv[4] / 1000.0f, fy = g_pv[5] / 1000.0f;
    float gx = g_rx - 1.0f, gy = g_ry;                     /* stand 1 m beside the remote player */
    float d2 = fdist2(fx, fy, g_rx, g_ry);
    if (d2 > 15.0f * 15.0f) { net_order(gx, gy, g_rz, 0, 1); g_ordX = gx; g_ordY = gy; g_haveOrder = 1; return; }
    if (d2 <= 1.6f * 1.6f) { g_stuckSince = 0; return; }
    /* Stuck: more than 3 m away and the distance has not shrunk by 0.3 m for 2 s (e.g. pathing
       blocked by fences): jump next to the remote player instead of walking. */
    if (d2 > 3.0f * 3.0f) {
        float d = 0; { float lo = 0, hi = 20; for (int it = 0; it < 24; it++) { float m = (lo + hi) / 2; if (m * m < d2) lo = m; else hi = m; } d = lo; }
        if (!g_stuckSince || d < g_bestD - 0.3f) { g_stuckSince = now; g_bestD = d; }
        else if (now - g_stuckSince > 2000) {
            net_order(gx, gy, g_rz, 0, 1); g_ordX = gx; g_ordY = gy; g_haveOrder = 1; g_stuckJumps++; g_stuckSince = 0; return;
        }
    } else g_stuckSince = 0;
    if (!g_haveOrder || fdist2(gx, gy, g_ordX, g_ordY) > 0.5f * 0.5f) {
        net_order(gx, gy, g_rz, d2 > 16.0f, 0); g_ordX = gx; g_ordY = gy; g_haveOrder = 1;
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
    s_cat(out, ",\"stuckJumps\":"); u2s(a, g_stuckJumps); s_cat(out, a);
    s_cat(out, ",\"vmBusySkips\":"); u2s(a, g_vmBusySkips); s_cat(out, a);
    s_cat(out, ",\"remoteMm\":["); i2s(a, g_rxm); s_cat(out, a); s_cat(out, ",");
    i2s(a, g_rym); s_cat(out, a); s_cat(out, ","); i2s(a, g_rzm); s_cat(out, a); s_cat(out, "]");
    s_cat(out, ",\"lastRecvAgoMs\":"); if (g_lastRecvTick) { u2s(a, GetTickCount() - g_lastRecvTick); s_cat(out, a); } else s_cat(out, "-1");
    s_cat(out, "}}");
}
