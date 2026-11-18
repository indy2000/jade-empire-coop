/*
 * steam_api_mock.c — test stand-in for steam_api.dll (32-bit) so two game copies on ONE PC can run the
 * Steam transport (one Steam account cannot message itself). Same file name and flat exports as the
 * subset jebridge uses. Config: steam_mock.txt next to this DLL:
 *     id <steamid64>          this player's fake SteamID (e.g. 900000001)
 *     udp <port>              local UDP port (127.0.0.1)
 *     peer_id <steamid64>     the other copy's fake SteamID
 *     peer_udp <port>         the other copy's UDP port
 *     owner <steamid64>       lobby owner (the host's id)
 *
 * Behaviour copied from real Steam where jebridge depends on it (playbook/networking-steam-spacewar.md):
 *  - CreateLobby/JoinLobby complete through callback 703 + GetAPICallResult; CreateLobby also broadcasts 513
 *    (real Steam delivers LobbyCreated twice).
 *  - The first message from a sender that has not been accepted (and that we never sent to) is held, and a
 *    SessionRequest (1251, identity at offset 0) is raised; it is delivered only after AcceptSessionWithUser.
 *  - Received message layout: data @0, size @4, sender identity @12 (as in the real 32-bit DLL).
 *  - Lobby members: the owner, plus the other id once it has been heard from (or we are the joiner).
 * Freestanding C (no CRT). License: MIT.
 */
#include <stdint.h>
#include <stddef.h>
#define WINAPI __stdcall
#define EXP __declspec(dllexport)
typedef void *HANDLE; typedef unsigned long DWORD; typedef int BOOL; typedef void *HMODULE; typedef uintptr_t SOCKET;
typedef uint64_t u64;
typedef struct { short f; unsigned short port; unsigned long addr; char z[8]; } SA4;
__declspec(dllimport) DWORD WINAPI GetModuleFileNameA(HMODULE, char*, DWORD);
__declspec(dllimport) HANDLE WINAPI CreateFileA(const char*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
__declspec(dllimport) BOOL WINAPI ReadFile(HANDLE, void*, DWORD, DWORD*, void*);
__declspec(dllimport) BOOL WINAPI WriteFile(HANDLE, const void*, DWORD, DWORD*, void*);
__declspec(dllimport) BOOL WINAPI CloseHandle(HANDLE);
__declspec(dllimport) HANDLE WINAPI GetProcessHeap(void);
__declspec(dllimport) void *WINAPI HeapAlloc(HANDLE, DWORD, size_t);
__declspec(dllimport) BOOL WINAPI HeapFree(HANDLE, DWORD, void*);
__declspec(dllimport) int __stdcall WSAStartup(unsigned short, void*);
__declspec(dllimport) SOCKET __stdcall socket(int, int, int);
__declspec(dllimport) int __stdcall bind(SOCKET, const void*, int);
__declspec(dllimport) int __stdcall sendto(SOCKET, const char*, int, int, const void*, int);
__declspec(dllimport) int __stdcall recvfrom(SOCKET, char*, int, int, void*, int*);
__declspec(dllimport) int __stdcall ioctlsocket(SOCKET, long, unsigned long*);

int _fltused = 0;
void *memset(void *d, int c, size_t n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { unsigned char *a = d; const unsigned char *b = s; while (n--) *a++ = *b++; return d; }
static size_t slen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static int pfx(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }
static u64 s2u64(const char *s) { u64 v = 0; while (*s >= '0' && *s <= '9') v = (v << 3) + (v << 1) + (u64)(*s++ - '0'); return v; }
static int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

static char g_dir[260];
static u64 g_id, g_peerId, g_owner;
static uint16_t g_udp, g_peerUdp;
static SOCKET g_s = (SOCKET)~0;
static int g_init;

/* ---- callbacks ---- */
typedef struct { int32_t hUser; int32_t callback; uint8_t *param; int32_t size; } CallbackMsg;
typedef struct { int id; uint8_t data[160]; int size; } QCb;
static QCb g_q[32]; static int g_qn, g_qcur = -1;
static void push_cb(int id, const void *data, int size) { if (g_qn < 32) { g_q[g_qn].id = id; memcpy(g_q[g_qn].data, data, (size_t)size); g_q[g_qn].size = size; g_qn++; } }
typedef struct { u64 call; int cbid; uint8_t data[64]; int size; } CallRes;
static CallRes g_res[8]; static int g_resn; static u64 g_nextCall = 0x1000;
static u64 add_result(int cbid, const void *data, int size) {
    u64 h = g_nextCall++;
    if (g_resn < 8) { g_res[g_resn].call = h; g_res[g_resn].cbid = cbid; memcpy(g_res[g_resn].data, data, (size_t)size); g_res[g_resn].size = size; g_resn++; }
    uint8_t done[16]; memcpy(done, &h, 8); memcpy(done + 8, &cbid, 4); uint32_t ps = (uint32_t)size; memcpy(done + 12, &ps, 4);
    push_cb(703, done, 16);
    return h;
}

/* ---- lobby ---- */
static u64 g_lobby; static int g_heardPeer;
static const u64 LOBBY_ID = 0x0186000000000001ull;

/* ---- messages ---- */
typedef struct { void *data; int32_t size; int32_t conn; uint8_t identity[136]; } Msg;  /* identity at offset 12 */
static uint8_t g_held[16][64]; static int g_heldLen[16]; static int g_heldn;
static int g_accepted, g_sentToPeer, g_reqPending;

static void load_config(void) {
    char p[300]; int n = (int)slen(g_dir); memcpy(p, g_dir, (size_t)n); memcpy(p + n, "steam_mock.txt", 15);
    HANDLE h = CreateFileA(p, 0x80000000, 1, 0, 3, 0x80, 0); if (h == (HANDLE)-1) return;
    char buf[512]; DWORD got = 0; ReadFile(h, buf, sizeof buf - 1, &got, 0); CloseHandle(h); buf[got] = 0;
    for (char *l = buf; *l; ) {
        char *e = l; while (*e && *e != '\n' && *e != '\r') e++; char sv = *e; *e = 0;
        if (pfx(l, "id ")) g_id = s2u64(l + 3);
        else if (pfx(l, "udp ")) g_udp = (uint16_t)s2u64(l + 4);
        else if (pfx(l, "peer_id ")) g_peerId = s2u64(l + 8);
        else if (pfx(l, "peer_udp ")) g_peerUdp = (uint16_t)s2u64(l + 9);
        else if (pfx(l, "owner ")) g_owner = s2u64(l + 6);
        *e = sv; l = *e ? e + 1 : e;
    }
}

BOOL WINAPI DllMain(HMODULE mod, DWORD reason, void *res) {
    (void)res;
    if (reason == 1) { GetModuleFileNameA(mod, g_dir, sizeof g_dir); size_t n = slen(g_dir); while (n && g_dir[n-1] != '\\') n--; g_dir[n] = 0; }
    return 1;
}

EXP int __cdecl SteamAPI_IsSteamRunning(void) { return 1; }
EXP int __cdecl SteamAPI_InitFlat(char *err) {
    load_config();
    if (!g_id || !g_udp || !g_peerUdp) { const char *m = "mock: steam_mock.txt missing id/udp/peer_udp"; memcpy(err, m, slen(m) + 1); return 1; }
    char wsa[400]; WSAStartup(0x202, wsa);
    g_s = socket(2, 2, 17);
    SA4 a; memset(&a, 0, sizeof a); a.f = 2; a.addr = 0x0100007F; a.port = (unsigned short)((g_udp >> 8) | (g_udp << 8));
    if (bind(g_s, &a, sizeof a) != 0) { const char *m = "mock: bind failed"; memcpy(err, m, slen(m) + 1); return 1; }
    unsigned long nb = 1; ioctlsocket(g_s, (long)0x8004667E, &nb);
    g_init = 1; return 0;
}
EXP void __cdecl SteamAPI_Shutdown(void) {}
EXP int __cdecl SteamAPI_GetHSteamPipe(void) { return 1; }
EXP void __cdecl SteamAPI_ManualDispatch_Init(void) {}
EXP void __cdecl SteamAPI_ManualDispatch_RunFrame(int pipe) { (void)pipe; }
EXP int __cdecl SteamAPI_ManualDispatch_GetNextCallback(int pipe, CallbackMsg *m) {
    (void)pipe;
    if (g_qcur >= 0 || g_qn == 0) return 0;
    g_qcur = 0; m->hUser = 1; m->callback = g_q[0].id; m->param = g_q[0].data; m->size = g_q[0].size; return 1;
}
EXP void __cdecl SteamAPI_ManualDispatch_FreeLastCallback(int pipe) {
    (void)pipe; if (g_qcur < 0) return;
    for (int i = 1; i < g_qn; i++) g_q[i - 1] = g_q[i];
    g_qn--; g_qcur = -1;
}
EXP int __cdecl SteamAPI_ManualDispatch_GetAPICallResult(int pipe, u64 call, void *buf, int size, int cbid, uint8_t *failed) {
    (void)pipe;
    for (int i = 0; i < g_resn; i++) if (g_res[i].call == call && g_res[i].cbid == cbid && g_res[i].size <= size) {
        memcpy(buf, g_res[i].data, (size_t)g_res[i].size); *failed = 0; return 1; }
    *failed = 1; return 0;
}
static int iface_dummy;
EXP void *__cdecl SteamAPI_SteamUser_v023(void) { return &iface_dummy; }
EXP void *__cdecl SteamAPI_SteamMatchmaking_v009(void) { return &iface_dummy; }
EXP void *__cdecl SteamAPI_SteamFriends_v018(void) { return &iface_dummy; }
EXP void *__cdecl SteamAPI_SteamNetworkingMessages_SteamAPI_v002(void) { return &iface_dummy; }
EXP u64 __cdecl SteamAPI_ISteamUser_GetSteamID(void *s) { (void)s; return g_id; }
EXP int __cdecl SteamAPI_ISteamUser_BLoggedOn(void *s) { (void)s; return 1; }

EXP u64 __cdecl SteamAPI_ISteamMatchmaking_CreateLobby(void *s, int type, int max) {
    (void)s; (void)type; (void)max;
    uint8_t lc[16]; memset(lc, 0, 16); int32_t ok = 1; memcpy(lc, &ok, 4); memcpy(lc + 8, &LOBBY_ID, 8);
    g_lobby = LOBBY_ID; g_owner = g_id;
    u64 h = add_result(513, lc, 16);
    push_cb(513, lc, 16);                       /* real Steam also broadcasts LobbyCreated */
    return h;
}
EXP u64 __cdecl SteamAPI_ISteamMatchmaking_JoinLobby(void *s, u64 lobby) {
    (void)s;
    uint8_t le[24]; memset(le, 0, 24); memcpy(le, &lobby, 8);
    uint32_t resp = (lobby == LOBBY_ID) ? 1 : 2; memcpy(le + 16, &resp, 4);
    if (resp == 1) g_lobby = lobby;
    return add_result(504, le, 24);
}
EXP void __cdecl SteamAPI_ISteamMatchmaking_LeaveLobby(void *s, u64 lobby) { (void)s; (void)lobby; g_lobby = 0; }
static char g_ldKey[32], g_ldVal[64];
EXP int __cdecl SteamAPI_ISteamMatchmaking_SetLobbyData(void *s, u64 lobby, const char *k, const char *v) {
    (void)s; (void)lobby; size_t a = slen(k), b = slen(v); if (a < 31 && b < 63) { memcpy(g_ldKey, k, a + 1); memcpy(g_ldVal, v, b + 1); } return 1; }
EXP const char *__cdecl SteamAPI_ISteamMatchmaking_GetLobbyData(void *s, u64 lobby, const char *k) {
    (void)s; (void)lobby; if (streq(k, "jecoop")) return "1"; if (streq(k, g_ldKey)) return g_ldVal; return ""; }
EXP int __cdecl SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(void *s, u64 lobby) {
    (void)s; if (!g_lobby || lobby != g_lobby) return 0;
    return (g_owner == g_id) ? (g_heardPeer ? 2 : 1) : 2;      /* joiner: owner + self */
}
EXP u64 __cdecl SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(void *s, u64 lobby, int i) {
    (void)s; (void)lobby; if (i == 0) return g_owner; if (i == 1) return (g_owner == g_id) ? g_peerId : g_id; return 0; }
EXP u64 __cdecl SteamAPI_ISteamMatchmaking_GetLobbyOwner(void *s, u64 lobby) { (void)s; (void)lobby; return g_owner; }
EXP int __cdecl SteamAPI_ISteamMatchmaking_InviteUserToLobby(void *s, u64 lobby, u64 who) { (void)s; (void)lobby; (void)who; return 1; }
EXP int __cdecl SteamAPI_ISteamFriends_SetRichPresence(void *s, const char *k, const char *v) { (void)s; (void)k; (void)v; return 1; }
EXP void __cdecl SteamAPI_ISteamFriends_ClearRichPresence(void *s) { (void)s; }

EXP void __cdecl SteamAPI_SteamNetworkingIdentity_SetSteamID64(uint8_t *id, u64 v) { memset(id, 0, 136); id[0] = 16; memcpy(id + 8, &v, 8); }
EXP u64 __cdecl SteamAPI_SteamNetworkingIdentity_GetSteamID64(const uint8_t *id) { u64 v; memcpy(&v, id + 8, 8); return id[0] == 16 ? v : 0; }

EXP int __cdecl SteamAPI_ISteamNetworkingMessages_SendMessageToUser(void *s, const uint8_t *id, const void *data, uint32_t size, int flags, int ch) {
    (void)s; (void)flags; (void)ch;
    if (!g_init || size > 1200) return 2;
    u64 to; memcpy(&to, id + 8, 8); if (to != g_peerId) return 2;
    char pkt[1300]; memcpy(pkt, &g_id, 8); memcpy(pkt + 8, data, size);
    SA4 a; memset(&a, 0, sizeof a); a.f = 2; a.addr = 0x0100007F; a.port = (unsigned short)((g_peerUdp >> 8) | (g_peerUdp << 8));
    g_sentToPeer = 1;                                  /* sending opens our side of the session */
    return sendto(g_s, pkt, (int)size + 8, 0, &a, sizeof a) == (int)size + 8 ? 1 : 2;
}
EXP int __cdecl SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser(void *s, const uint8_t *id) {
    (void)s; u64 v; memcpy(&v, id + 8, 8); if (v == g_peerId) { g_accepted = 1; g_reqPending = 0; return 1; } return 0; }
EXP int __cdecl SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser(void *s, const uint8_t *id) { (void)s; (void)id; g_accepted = 0; return 1; }
EXP void __cdecl SteamAPI_SteamNetworkingMessage_t_Release(Msg *m) { if (m) HeapFree(GetProcessHeap(), 0, m); }
static Msg *make_msg(const uint8_t *data, int size, u64 from) {
    Msg *m = HeapAlloc(GetProcessHeap(), 8 /*ZERO*/, sizeof(Msg) + (size_t)size);
    if (!m) return 0;
    m->data = (uint8_t *)(m + 1); m->size = size; memcpy(m->data, data, (size_t)size);
    SteamAPI_SteamNetworkingIdentity_SetSteamID64(m->identity, from);
    return m;
}
EXP int __cdecl SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel(void *s, int ch, Msg **out, int max) {
    (void)s; (void)ch;
    if (!g_init || max < 1) return 0;
    int n = 0;
    if ((g_accepted || g_sentToPeer) && g_heldn) {           /* release held messages after accept */
        Msg *m = make_msg(g_held[0], g_heldLen[0], g_peerId);
        for (int i = 1; i < g_heldn; i++) { memcpy(g_held[i - 1], g_held[i], 64); g_heldLen[i - 1] = g_heldLen[i]; }
        g_heldn--; if (m) { out[n++] = m; return n; }
    }
    char pkt[1300]; SA4 from; int fl = sizeof from;
    int r = recvfrom(g_s, pkt, sizeof pkt, 0, &from, &fl);
    if (r <= 8) return 0;
    u64 sender; memcpy(&sender, pkt, 8);
    if (sender != g_peerId) return 0;
    g_heardPeer = 1;
    if (!g_accepted && !g_sentToPeer) {                        /* stranger: hold + SessionRequest */
        if (g_heldn < 16 && r - 8 <= 64) { memcpy(g_held[g_heldn], pkt + 8, (size_t)(r - 8)); g_heldLen[g_heldn] = r - 8; g_heldn++; }
        if (!g_reqPending) { uint8_t ident[136]; SteamAPI_SteamNetworkingIdentity_SetSteamID64(ident, sender); push_cb(1251, ident, 136); g_reqPending = 1; }
        return 0;
    }
    Msg *m = make_msg((uint8_t *)pkt + 8, r - 8, sender);
    if (m) out[n++] = m;
    return n;
}
