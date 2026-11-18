/* steamnet.inc — Steam (AppID 480 "Spacewar") transport for netsync (included by jebridge.c).
 *
 * net.txt:  transport steam
 *           role host            -> creates a friends-only lobby (id written to bridge\lobby.txt)
 *           role join            -> joins the lobby given by "lobby <id>" (decimal)
 *           lobby <id>
 *
 * steam_api.dll (32-bit, Steamworks.NET Standalone 2025.164.1, SHA-256 fdafb7c6…) is loaded from the
 * game folder with LoadLibrary; only the flat C API is used (no SDK headers). For tests on one PC the
 * same file name can be the mock (mock/steam_api_mock.c). steam_appid.txt ("480") is written if missing.
 *
 * Rules from playbook/networking-steam-spacewar.md: manual callback dispatch; LobbyCreated may arrive
 * twice (call result 703 and broadcast 513); accept sessions only from lobby members (host) / the
 * lobby owner (joiner); messages: data @0, size @4, sender identity @12 (fallback @16) on 32-bit.
 */

typedef uint64_t u64;
typedef struct { int32_t hSteamUser; int32_t callback; uint8_t *param; int32_t param_size; } CallbackMsg;
typedef struct { uint8_t b[160]; } SteamIdentity;           /* 136 bytes in the SDK; opaque */

/* flat API (cdecl) */
static int  (__cdecl *sa_InitFlat)(char *);
static int  (__cdecl *sa_IsSteamRunning)(void);
static int  (__cdecl *sa_GetHSteamPipe)(void);
static void (__cdecl *sa_MD_Init)(void);
static void (__cdecl *sa_MD_RunFrame)(int);
static int  (__cdecl *sa_MD_GetNextCallback)(int, CallbackMsg *);
static void (__cdecl *sa_MD_FreeLastCallback)(int);
static int  (__cdecl *sa_MD_GetAPICallResult)(int, u64, void *, int, int, uint8_t *);
static void*(__cdecl *sa_SteamUser)(void);
static void*(__cdecl *sa_SteamMatchmaking)(void);
static void*(__cdecl *sa_SteamFriends)(void);
static void*(__cdecl *sa_SteamNetMsgs)(void);
static u64  (__cdecl *sa_GetSteamID)(void *);
static int  (__cdecl *sa_BLoggedOn)(void *);
static u64  (__cdecl *sa_CreateLobby)(void *, int, int);
static u64  (__cdecl *sa_JoinLobby)(void *, u64);
static void (__cdecl *sa_LeaveLobby)(void *, u64);
static int  (__cdecl *sa_SetLobbyData)(void *, u64, const char *, const char *);
static const char *(__cdecl *sa_GetLobbyData)(void *, u64, const char *);
static int  (__cdecl *sa_GetNumLobbyMembers)(void *, u64);
static u64  (__cdecl *sa_GetLobbyMemberByIndex)(void *, u64, int);
static u64  (__cdecl *sa_GetLobbyOwner)(void *, u64);
static int  (__cdecl *sa_SetRichPresence)(void *, const char *, const char *);
static void (__cdecl *sa_IdSet)(SteamIdentity *, u64);
static u64  (__cdecl *sa_IdGet)(const SteamIdentity *);
static int  (__cdecl *sa_SendMessageToUser)(void *, const SteamIdentity *, const void *, uint32_t, int, int);
static int  (__cdecl *sa_ReceiveMessagesOnChannel)(void *, int, void **, int);
static int  (__cdecl *sa_AcceptSessionWithUser)(void *, const SteamIdentity *);
static void (__cdecl *sa_MsgRelease)(void *);

static void *g_sUser, *g_sMM, *g_sFriends, *g_sMsgs;
static int g_sPipe, g_steamOn, g_steamHost;
static u64 g_me, g_lobby, g_wantLobby, g_speer, g_createCall, g_joinCall;
static int g_lobbyState;          /* 0 none, 1 creating/joining, 2 in lobby, 3 failed */
static uint32_t g_sSent, g_sRecv, g_sAccepted, g_sRejected, g_sSendErr, g_sOffset12, g_sOffset16, g_sLastPeerScan;
static char g_steamErr[96];

static void *sp(HMODULE h, const char *n) { void *p = GetProcAddress(h, n); if (!p) { s_cpy(g_steamErr, "missing export "); s_cat(g_steamErr, n); } return p; }
/* 64-bit decimal conversion without the CRT's __aulldiv/__allmul helpers. */
static uint32_t u64_div10(u64 *v) {
    uint32_t p[4] = { (uint32_t)(*v >> 48) & 0xFFFF, (uint32_t)(*v >> 32) & 0xFFFF, (uint32_t)(*v >> 16) & 0xFFFF, (uint32_t)*v & 0xFFFF };
    uint32_t r = 0;
    for (int i = 0; i < 4; i++) { uint32_t cur = (r << 16) | p[i]; p[i] = cur / 10; r = cur % 10; }
    *v = ((u64)p[0] << 48) | ((u64)p[1] << 32) | ((u64)p[2] << 16) | (u64)p[3];
    return r;
}
static void u64s(char *d, u64 v) { char t[24]; int i = 0; do { t[i++] = (char)('0' + u64_div10(&v)); } while (v); while (i) *d++ = t[--i]; *d = 0; }
static u64 s2u64(const char *s) { u64 v = 0; while (*s >= '0' && *s <= '9') v = (v << 3) + (v << 1) + (u64)(*s++ - '0'); return v; }

static int steam_load(void) {
    char p[300]; s_cpy(p, g_dir); s_cat(p, "steam_appid.txt");
    HANDLE h = CreateFileA(p, 0x80000000, 1, 0, 3, 0x80, 0);
    if (h == (HANDLE)-1) { h = CreateFileA(p, 0x40000000, 0, 0, 1 /*CREATE_NEW*/, 0x80, 0); if (h != (HANDLE)-1) { DWORD w; WriteFile(h, "480", 3, &w, 0); CloseHandle(h); } }
    else CloseHandle(h);
    s_cpy(p, g_dir); s_cat(p, "steam_api.dll");
    HMODULE m = LoadLibraryA(p);
    if (!m) { s_cpy(g_steamErr, "steam_api.dll not found next to the game"); return 0; }
    g_steamErr[0] = 0;
    sa_InitFlat = sp(m, "SteamAPI_InitFlat"); sa_IsSteamRunning = sp(m, "SteamAPI_IsSteamRunning"); sa_GetHSteamPipe = sp(m, "SteamAPI_GetHSteamPipe");
    sa_MD_Init = sp(m, "SteamAPI_ManualDispatch_Init"); sa_MD_RunFrame = sp(m, "SteamAPI_ManualDispatch_RunFrame");
    sa_MD_GetNextCallback = sp(m, "SteamAPI_ManualDispatch_GetNextCallback"); sa_MD_FreeLastCallback = sp(m, "SteamAPI_ManualDispatch_FreeLastCallback");
    sa_MD_GetAPICallResult = sp(m, "SteamAPI_ManualDispatch_GetAPICallResult");
    sa_SteamUser = sp(m, "SteamAPI_SteamUser_v023"); sa_SteamMatchmaking = sp(m, "SteamAPI_SteamMatchmaking_v009");
    sa_SteamFriends = sp(m, "SteamAPI_SteamFriends_v018"); sa_SteamNetMsgs = sp(m, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002");
    sa_GetSteamID = sp(m, "SteamAPI_ISteamUser_GetSteamID"); sa_BLoggedOn = sp(m, "SteamAPI_ISteamUser_BLoggedOn");
    sa_CreateLobby = sp(m, "SteamAPI_ISteamMatchmaking_CreateLobby"); sa_JoinLobby = sp(m, "SteamAPI_ISteamMatchmaking_JoinLobby");
    sa_LeaveLobby = sp(m, "SteamAPI_ISteamMatchmaking_LeaveLobby"); sa_SetLobbyData = sp(m, "SteamAPI_ISteamMatchmaking_SetLobbyData");
    sa_GetLobbyData = sp(m, "SteamAPI_ISteamMatchmaking_GetLobbyData"); sa_GetNumLobbyMembers = sp(m, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers");
    sa_GetLobbyMemberByIndex = sp(m, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex"); sa_GetLobbyOwner = sp(m, "SteamAPI_ISteamMatchmaking_GetLobbyOwner");
    sa_SetRichPresence = sp(m, "SteamAPI_ISteamFriends_SetRichPresence");
    sa_IdSet = sp(m, "SteamAPI_SteamNetworkingIdentity_SetSteamID64"); sa_IdGet = sp(m, "SteamAPI_SteamNetworkingIdentity_GetSteamID64");
    sa_SendMessageToUser = sp(m, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser");
    sa_ReceiveMessagesOnChannel = sp(m, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel");
    sa_AcceptSessionWithUser = sp(m, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser");
    sa_MsgRelease = sp(m, "SteamAPI_SteamNetworkingMessage_t_Release");
    return g_steamErr[0] == 0;
}

/* Called once on the main thread when net is armed (Steam wants init and callbacks on one thread). */
static int steam_start(void) {
    if (!steam_load()) { logline(g_steamErr); return 0; }
    if (!sa_IsSteamRunning()) { s_cpy(g_steamErr, "Steam is not running"); logline(g_steamErr); return 0; }
    char err[1024]; err[0] = 0;
    int rc = sa_InitFlat(err);
    if (rc != 0) { s_cpy(g_steamErr, "SteamAPI_InitFlat failed: "); int n = (int)s_len(g_steamErr); for (int i = 0; err[i] && n < 94; i++) g_steamErr[n++] = err[i]; g_steamErr[n] = 0; logline(g_steamErr); return 0; }
    sa_MD_Init(); g_sPipe = sa_GetHSteamPipe();
    g_sUser = sa_SteamUser(); g_sMM = sa_SteamMatchmaking(); g_sFriends = sa_SteamFriends(); g_sMsgs = sa_SteamNetMsgs();
    if (!g_sUser || !g_sMM || !g_sMsgs) { s_cpy(g_steamErr, "Steam interfaces missing"); logline(g_steamErr); return 0; }
    g_me = sa_GetSteamID(g_sUser);
    char m[120], a[24]; s_cpy(m, "net: steam up, my id "); u64s(a, g_me); s_cat(m, a); s_cat(m, g_steamHost ? ", hosting" : ", joining"); logline(m);
    if (g_steamHost) { g_createCall = sa_CreateLobby(g_sMM, 1 /* friends-only */, 2); g_lobbyState = 1; }
    else if (g_wantLobby) { g_joinCall = sa_JoinLobby(g_sMM, g_wantLobby); g_lobbyState = 1; }
    else { s_cpy(g_steamErr, "role join needs 'lobby <id>' in net.txt"); logline(g_steamErr); g_lobbyState = 3; }
    g_steamOn = 1;
    return 1;
}

static void steam_write_lobby_file(void) {
    char p[300], t[32]; s_cpy(p, g_dir); s_cat(p, "bridge\\lobby.txt"); u64s(t, g_lobby); s_cat(t, "\r\n");
    HANDLE h = CreateFileA(p, 0x40000000, 1, 0, 2, 0x80, 0); if (h != (HANDLE)-1) { DWORD w; WriteFile(h, t, (DWORD)s_len(t), &w, 0); CloseHandle(h); }
}
static void steam_entered(u64 lobby, int host) {
    if (g_lobbyState == 2 && g_lobby == lobby) return;            /* LobbyCreated arrives twice on real Steam */
    g_lobby = lobby; g_lobbyState = 2;
    char m[120], a[24]; s_cpy(m, host ? "net: lobby created " : "net: lobby joined "); u64s(a, lobby); s_cat(m, a); logline(m);
    if (host) {
        sa_SetLobbyData(g_sMM, lobby, "jecoop", "1");
        if (sa_SetRichPresence && g_sFriends) { char c[48]; s_cpy(c, "+connect_lobby "); s_cat(c, a); sa_SetRichPresence(g_sFriends, "connect", c); }
        steam_write_lobby_file();
    }
}
static int steam_is_member(u64 id) {
    if (!g_lobby) return 0;
    int n = sa_GetNumLobbyMembers(g_sMM, g_lobby);
    for (int i = 0; i < n && i < 8; i++) if (sa_GetLobbyMemberByIndex(g_sMM, g_lobby, i) == id) return 1;
    return 0;
}
static void steam_scan_peer(void) {           /* two players: the peer is the other lobby member */
    if (g_lobbyState != 2) return;
    int n = sa_GetNumLobbyMembers(g_sMM, g_lobby); u64 found = 0;
    for (int i = 0; i < n && i < 8; i++) { u64 id = sa_GetLobbyMemberByIndex(g_sMM, g_lobby, i); if (id && id != g_me) { found = id; break; } }
    if (found != g_speer) { g_speer = found; char m[80], a[24]; s_cpy(m, "net: peer "); u64s(a, found); s_cat(m, a); logline(m); }
}

/* Per frame: callbacks. */
static void steam_pump(void) {
    if (!g_steamOn) return;
    sa_MD_RunFrame(g_sPipe);
    CallbackMsg cb;
    while (sa_MD_GetNextCallback(g_sPipe, &cb)) {
        if (cb.callback == 703 && cb.param) {
            u64 call; int32_t which; uint32_t psize;
            memcpy(&call, cb.param, 8); memcpy(&which, cb.param + 8, 4); memcpy(&psize, cb.param + 12, 4);
            uint8_t buf[64]; uint8_t failed = 0;
            if (psize <= sizeof buf && sa_MD_GetAPICallResult(g_sPipe, call, buf, (int)psize, which, &failed) && !failed) {
                if (which == 513) { int32_t res; u64 lob; memcpy(&res, buf, 4); memcpy(&lob, buf + 8, 8);
                                    if (res == 1) steam_entered(lob, 1); else { g_lobbyState = 3; logline("net: CreateLobby failed"); } }
                else if (which == 504) { u64 lob; uint32_t resp; memcpy(&lob, buf, 8); memcpy(&resp, buf + 16, 4);
                                    if (resp == 1) steam_entered(lob, g_steamHost); else { g_lobbyState = 3; logline("net: JoinLobby refused"); } }
            }
        } else if (cb.callback == 513 && cb.param) {
            int32_t res; u64 lob; memcpy(&res, cb.param, 4); memcpy(&lob, cb.param + 8, 8); if (res == 1) steam_entered(lob, 1);
        } else if (cb.callback == 504 && cb.param) {
            u64 lob; uint32_t resp; memcpy(&lob, cb.param, 8); memcpy(&resp, cb.param + 16, 4); if (resp == 1) steam_entered(lob, g_steamHost);
        } else if (cb.callback == 1251 && cb.param) {           /* SessionRequest: identity at offset 0 */
            u64 who = sa_IdGet((const SteamIdentity *)cb.param);
            int ok = g_steamHost ? steam_is_member(who) : (g_lobby && who == sa_GetLobbyOwner(g_sMM, g_lobby));
            if (ok) { sa_AcceptSessionWithUser(g_sMsgs, (const SteamIdentity *)cb.param); g_sAccepted++; }
            else g_sRejected++;
        }
        sa_MD_FreeLastCallback(g_sPipe);
    }
    uint32_t now = GetTickCount();
    if (now - g_sLastPeerScan > 500) { g_sLastPeerScan = now; steam_scan_peer(); }
}
static void steam_send(const char *pkt, int len) {
    if (!g_steamOn || !g_speer) return;
    SteamIdentity id; memset(&id, 0, sizeof id); sa_IdSet(&id, g_speer);
    int r = sa_SendMessageToUser(g_sMsgs, &id, pkt, (uint32_t)len, 0 /*Unreliable*/ | 1 /*NoNagle*/, 0);
    if (r == 1 /* k_EResultOK */) g_sSent++; else g_sSendErr++;
}
/* Returns bytes copied into buf, 0 when nothing is waiting. Drops messages from anyone but the peer. */
static int steam_recv(char *buf, int max) {
    if (!g_steamOn) return 0;
    for (;;) {
        void *msgs[1] = { 0 };
        if (sa_ReceiveMessagesOnChannel(g_sMsgs, 0, msgs, 1) <= 0) return 0;
        uint8_t *m = (uint8_t *)msgs[0];
        void *data; int32_t size; memcpy(&data, m, 4); memcpy(&size, m + 4, 4);
        u64 from = sa_IdGet((const SteamIdentity *)(m + 12));
        if (from) g_sOffset12++; else { from = sa_IdGet((const SteamIdentity *)(m + 16)); if (from) g_sOffset16++; }
        int n = 0;
        if (from && from == g_speer && size > 0 && size <= max) { memcpy(buf, data, (size_t)size); n = size; g_sRecv++; }
        sa_MsgRelease(m);
        if (n) return n;
    }
}
static void steam_status(char *out) {
    char a[24];
    s_cat(out, ",\"steam\":{\"on\":"); s_cat(out, g_steamOn ? "true" : "false");
    s_cat(out, ",\"host\":"); s_cat(out, g_steamHost ? "true" : "false");
    s_cat(out, ",\"me\":\""); u64s(a, g_me); s_cat(out, a);
    s_cat(out, "\",\"lobby\":\""); u64s(a, g_lobby); s_cat(out, a);
    s_cat(out, "\",\"lobbyState\":"); u2s(a, (uint32_t)g_lobbyState); s_cat(out, a);
    s_cat(out, ",\"peer\":\""); u64s(a, g_speer); s_cat(out, a);
    s_cat(out, "\",\"sent\":"); u2s(a, g_sSent); s_cat(out, a);
    s_cat(out, ",\"recv\":"); u2s(a, g_sRecv); s_cat(out, a);
    s_cat(out, ",\"sendErr\":"); u2s(a, g_sSendErr); s_cat(out, a);
    s_cat(out, ",\"accepted\":"); u2s(a, g_sAccepted); s_cat(out, a);
    s_cat(out, ",\"rejected\":"); u2s(a, g_sRejected); s_cat(out, a);
    s_cat(out, ",\"senderAt12\":"); u2s(a, g_sOffset12); s_cat(out, a);
    s_cat(out, ",\"senderAt16\":"); u2s(a, g_sOffset16); s_cat(out, a);
    s_cat(out, ",\"error\":\""); s_cat(out, g_steamErr); s_cat(out, "\"}");
}
