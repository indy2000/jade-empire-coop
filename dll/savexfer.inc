/* savexfer.inc -- 0.9.0: the guest joins from the host's save (Steam transport only).
 *
 * net.txt (guest):  getsave 1      -> ask the host for its save until one arrives
 *
 * Flow (all on Steam channel 1, reliable):
 *   guest -> host  'JEQS'                         request (every 2 s while waiting; works from the main menu)
 *   host           DoAutoSave (#514), waits for the new "Save\Auto*" folder (<= 10 s)
 *   host -> guest  'JESH' xid nfiles {name[32] size crc32} x nfiles      header
 *   host -> guest  'JESD' xid file offset data[<= 8192]                    chunks, paced, resent on send error
 *   guest          checks size + CRC32 of every file, writes Save\Co-op - host\<files>,
 *                  two beeps, bridge\save_received.txt; the player loads it from the Load menu.
 * Files: save.sac (~200 KB) and SaveImage.tga (~1 MB, the menu picture). */

#define XF_MAXFILE (1600 * 1024)
#define XF_CHUNK   8192
static const char *const XF_FILES[2] = { "save.sac", "SaveImage.tga" };
static uint8_t g_xbuf[2][XF_MAXFILE];
static uint32_t g_xsize[2], g_xcrc[2], g_xgot[2], g_xid;
static int g_xstate;               /* host: 0 idle, 1 saving, 2 sending.  guest: 0 idle, 1 receiving */
static int g_xfile; static uint32_t g_xoff, g_xtick, g_xlastReq, g_xsaveTick;
static int g_getSave, g_saveDone; static uint32_t g_xSentFiles, g_xRecvFiles, g_xErrs, g_xReqs;
static char g_xdir[300];           /* host: the autosave folder being sent */
static uint32_t g_xprevHi, g_xprevLo;   /* host: newest Auto* folder time before the save */

static int mem4(const uint8_t *p, const char *t) { return p[0] != (uint8_t)t[0] || p[1] != (uint8_t)t[1] || p[2] != (uint8_t)t[2] || p[3] != (uint8_t)t[3]; }
static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1))); }
    return ~c;
}
typedef struct { DWORD attr; DWORD ct[2], at[2], wt[2]; DWORD sh, sl, r0, r1; char name[260]; char alt[14]; } WFD_;
/* newest "Save\Auto*" folder: full path into out (with trailing '\'), its write time; 0 if none */
static int xf_newest_auto(char *out, uint32_t *hi, uint32_t *lo) {
    char pat[300]; s_cpy(pat, g_dir); s_cat(pat, "Save\\Auto*");
    WFD_ f; HANDLE h = FindFirstFileA(pat, (void *)&f); if (h == (HANDLE)-1) return 0;
    int found = 0; *hi = *lo = 0;
    do {
        if (!(f.attr & 0x10)) continue;
        if (!found || f.wt[1] > *hi || (f.wt[1] == *hi && f.wt[0] > *lo)) {
            found = 1; *hi = f.wt[1]; *lo = f.wt[0]; s_cpy(out, g_dir); s_cat(out, "Save\\"); s_cat(out, f.name); s_cat(out, "\\");
        }
    } while (FindNextFileA(h, (void *)&f));
    FindClose(h); return found;
}
static int xf_read(const char *path, uint8_t *buf, uint32_t max, uint32_t *n) {
    HANDLE h = CreateFileA(path, 0x80000000, 1, 0, 3, 0x80, 0); if (h == (HANDLE)-1) return 0;
    uint32_t tot = 0; DWORD r = 0;
    while (tot < max && ReadFile(h, buf + tot, (max - tot) > 65536 ? 65536 : (max - tot), &r, 0) && r) tot += r;
    CloseHandle(h); *n = tot; return tot < max;      /* a file that fills the buffer is too big */
}
static int xf_write(const char *path, const uint8_t *buf, uint32_t n) {
    HANDLE h = CreateFileA(path, 0x40000000, 0, 0, 2, 0x80, 0); if (h == (HANDLE)-1) return 0;
    DWORD w = 0; uint32_t tot = 0;
    while (tot < n && WriteFile(h, buf + tot, (n - tot) > 65536 ? 65536 : (n - tot), &w, 0) && w) tot += w;
    CloseHandle(h); return tot == n;
}
static int xf_send(const void *p, int n) {
    if (!g_steamOn || !g_speer) return 0;
    SteamIdentity id; memset(&id, 0, sizeof id); sa_IdSet(&id, g_speer);
    return sa_SendMessageToUser(g_sMsgs, &id, p, (uint32_t)n, 8 /*Reliable*/ | 1 /*NoNagle*/, 1) == 1;
}

/* host: start a transfer (called on a request, main thread, player present) */
static void xf_host_begin(void) {
    char tmp[300]; if (!xf_newest_auto(tmp, &g_xprevHi, &g_xprevLo)) g_xprevHi = g_xprevLo = 0;
    Ncs s; n_void_main_begin(&s); n_action(&s, 514 /* DoAutoSave */, 0); n_retn(&s); n_finish(&s);
    if (ms_store("jbn_save", s.b, s.n) || vm_run("jbn_save", 0x7F000000) != 1) { g_xErrs++; logline("save: DoAutoSave failed"); return; }
    g_xstate = 1; g_xsaveTick = GetTickCount(); logline("save: autosave requested for the guest");
}
static void xf_host_tick(uint32_t now) {
    if (g_xstate == 1) {                                  /* wait for a newer Auto* folder, then load it */
        uint32_t hi, lo; char d[300];
        if (now - g_xsaveTick > 10000) { g_xstate = 0; g_xErrs++; logline("save: no new autosave folder after 10 s"); return; }
        if (now - g_xsaveTick < 1500 || !xf_newest_auto(d, &hi, &lo) || (hi < g_xprevHi || (hi == g_xprevHi && lo <= g_xprevLo))) return;
        s_cpy(g_xdir, d);
        for (int f = 0; f < 2; f++) {
            char p[320]; s_cpy(p, g_xdir); s_cat(p, XF_FILES[f]);
            if (!xf_read(p, g_xbuf[f], XF_MAXFILE, &g_xsize[f]) || !g_xsize[f]) { g_xstate = 0; g_xErrs++; logline("save: cannot read the autosave files"); return; }
            g_xcrc[f] = crc32(g_xbuf[f], g_xsize[f]);
        }
        g_xid++; uint8_t h[8 + 4 + 2 * 40]; memset(h, 0, sizeof h); memcpy(h, "JESH", 4); memcpy(h + 4, &g_xid, 4);
        uint32_t nf = 2; memcpy(h + 8, &nf, 4);
        for (int f = 0; f < 2; f++) { uint8_t *e = h + 12 + 40 * f; s_cpy((char *)e, XF_FILES[f]); memcpy(e + 32, &g_xsize[f], 4); memcpy(e + 36, &g_xcrc[f], 4); }
        if (!xf_send(h, 12 + 80)) { g_xErrs++; return; }  /* retried on the next tick */
        g_xstate = 2; g_xfile = 0; g_xoff = 0; logline("save: sending the autosave to the guest");
        return;
    }
    if (g_xstate == 2) {                                  /* 4 chunks per tick (~32 KB), stop on a send error */
        for (int k = 0; k < 4; k++) {
            if (g_xfile >= 2) { g_xstate = 0; g_xSentFiles += 2; logline("save: sent"); return; }
            uint32_t n = g_xsize[g_xfile] - g_xoff; if (n > XF_CHUNK) n = XF_CHUNK;
            static uint8_t c[16 + XF_CHUNK]; memcpy(c, "JESD", 4); memcpy(c + 4, &g_xid, 4);
            uint32_t fi = (uint32_t)g_xfile; memcpy(c + 8, &fi, 4); memcpy(c + 12, &g_xoff, 4); memcpy(c + 16, g_xbuf[g_xfile] + g_xoff, n);
            if (!xf_send(c, 16 + (int)n)) { g_xErrs++; return; }    /* Steam's send buffer is full: retry later */
            g_xoff += n; if (g_xoff >= g_xsize[g_xfile]) { g_xfile++; g_xoff = 0; }
        }
    }
}
/* guest: all files complete and correct -> write them */
static void xf_guest_finish(void) {
    char d[300]; s_cpy(d, g_dir); s_cat(d, "Save\\Co-op - host\\"); CreateDirectoryA(d, 0);
    for (int f = 0; f < 2; f++) {
        if (g_xgot[f] != g_xsize[f] || crc32(g_xbuf[f], g_xsize[f]) != g_xcrc[f]) { g_xErrs++; g_xstate = 0; logline("save: received file failed its check, asking again"); return; }
    }
    for (int f = 0; f < 2; f++) { char p[320]; s_cpy(p, d); s_cat(p, XF_FILES[f]); if (!xf_write(p, g_xbuf[f], g_xsize[f])) { g_xErrs++; g_xstate = 0; logline("save: cannot write the save"); return; } }
    char m[320]; s_cpy(m, g_dir); s_cat(m, "bridge\\save_received.txt"); xf_write(m, (const uint8_t *)"Save\\Co-op - host\r\n", 19);
    g_xstate = 0; g_saveDone = 1; g_xRecvFiles += 2;
    logline("save: received the host's save -> Save\\Co-op - host (load it from the menu)");
    MessageBeep(0); Sleep(250); MessageBeep(0);
}
/* channel 1 messages, both roles */
static void xf_recv(uint32_t now) {
    for (int k = 0; k < 64; k++) {
        void *msgs[1] = { 0 };
        if (sa_ReceiveMessagesOnChannel(g_sMsgs, 1, msgs, 1) <= 0) return;
        uint8_t *m = (uint8_t *)msgs[0]; uint8_t *p; int32_t n; memcpy(&p, m, 4); memcpy(&n, m + 4, 4);
        u64 from = sa_IdGet((const SteamIdentity *)(m + 12)); if (!from) from = sa_IdGet((const SteamIdentity *)(m + 16));
        if (from == g_speer && n >= 8) {
            if (!mem4(p, "JEQS") && g_steamHost) { g_xReqs++; if (g_xstate == 0) { if (player_exists()) xf_host_begin(); else logline("save: guest asked, but the host has no game loaded yet"); } }
            else if (!mem4(p, "JEDM")) cb_on_damage(p, n);      /* 0.10.0: guest damage request */
            else if (!mem4(p, "JEEN")) cb_on_engage(p, n);      /* 0.11.0: enemy the guest fights + where it is */
            else if (!mem4(p, "JESH") && !g_steamHost && n >= 12 + 80) {
                memcpy(&g_xid, p + 4, 4);
                for (int f = 0; f < 2; f++) { memcpy(&g_xsize[f], p + 12 + 40 * f + 32, 4); memcpy(&g_xcrc[f], p + 12 + 40 * f + 36, 4); g_xgot[f] = 0; }
                if (g_xsize[0] > XF_MAXFILE || g_xsize[1] > XF_MAXFILE) g_xstate = 0; else { g_xstate = 1; g_xtick = now; logline("save: receiving the host's save"); }
            }
            else if (!mem4(p, "JESD") && !g_steamHost && g_xstate == 1 && n >= 16) {
                uint32_t id, fi, off; memcpy(&id, p + 4, 4); memcpy(&fi, p + 8, 4); memcpy(&off, p + 12, 4); uint32_t len = (uint32_t)n - 16;
                if (id == g_xid && fi < 2 && off == g_xgot[fi] && off + len <= g_xsize[fi]) {
                    memcpy(g_xbuf[fi] + off, p + 16, len); g_xgot[fi] += len; g_xtick = now;
                    if (g_xgot[0] == g_xsize[0] && g_xgot[1] == g_xsize[1]) xf_guest_finish();
                }
            }
        }
        sa_MsgRelease(m);
    }
    (void)now;
}
static void xf_tick(void) {
    if (!g_useSteam || !g_steamOn || !g_speer) return;
    uint32_t now = GetTickCount();
    xf_recv(now);
    if (g_steamHost) xf_host_tick(now);
    else if (g_xstate == 1 && now - g_xtick > 10000) { g_xstate = 0; g_xErrs++; logline("save: transfer stalled, asking again"); }
    else if (g_getSave && !g_saveDone && g_xstate == 0 && now - g_xlastReq > 2000) {
        g_xlastReq = now; uint8_t q[8]; memcpy(q, "JEQS", 4); memset(q + 4, 0, 4); if (xf_send(q, 8)) g_xReqs++;
    }
}
static void xf_status(char *out) {
    char a[16];
    s_cat(out, ",\"save\":{\"want\":"); s_cat(out, g_getSave ? "true" : "false");
    s_cat(out, ",\"done\":"); s_cat(out, g_saveDone ? "true" : "false");
    s_cat(out, ",\"state\":"); i2s(a, g_xstate); s_cat(out, a);
    s_cat(out, ",\"reqs\":"); u2s(a, g_xReqs); s_cat(out, a);
    s_cat(out, ",\"sentFiles\":"); u2s(a, g_xSentFiles); s_cat(out, a);
    s_cat(out, ",\"recvFiles\":"); u2s(a, g_xRecvFiles); s_cat(out, a);
    s_cat(out, ",\"got\":["); u2s(a, g_xgot[0]); s_cat(out, a); s_cat(out, ","); u2s(a, g_xgot[1]); s_cat(out, a);
    s_cat(out, "],\"size\":["); u2s(a, g_xsize[0]); s_cat(out, a); s_cat(out, ","); u2s(a, g_xsize[1]); s_cat(out, a);
    s_cat(out, "],\"errors\":"); u2s(a, g_xErrs); s_cat(out, a); s_cat(out, "}");
}
