/*
 * jebridge: in-game RPC bridge for Jade Empire co-op research (test installs only).
 * Speaks coop-anything bridge protocol v1 (tools/bridge/PROTOCOL.md) over TCP 127.0.0.1.
 * Loaded as a proxy xinput9_1_0.dll; real exports forward to xinput_orig.dll.
 *
 * Game addresses are for JadeEmpire.exe v1.00 build 1510,
 * SHA-256 0d25f18c028a7dffff9dd110b7be677f7e9b63e1cad86c1eb8e4e8e189c54fe5.
 * The DLL refuses to hook anything if the in-memory image does not match the
 * expected IAT signature (see check_game()).
 *
 * Freestanding: no CRT. Built with clang --target=i686-pc-windows-msvc + lld-link.
 * License: MIT.
 */
#include <stdint.h>
#include <stddef.h>

/* ---------------- minimal Win32 declarations ---------------- */
#define WINAPI __stdcall
typedef void *HANDLE; typedef unsigned long DWORD; typedef int BOOL; typedef void *HMODULE;
typedef uintptr_t SOCKET;
#define INVALID_SOCKET ((SOCKET)~0)
typedef struct { short sin_family; unsigned short sin_port; unsigned long addr; char zero[8]; } SOCKADDR_IN4;
typedef struct { DWORD a[100]; } WSADATA_;
typedef struct { void *DebugInfo; long LockCount; long RecursionCount; HANDLE OwningThread; HANDLE LockSemaphore; uintptr_t SpinCount; } CRITICAL_SECTION;
typedef struct { void *hwnd; unsigned msg; uintptr_t w; intptr_t l; DWORD time; long x, y; } MSG_;

__declspec(dllimport) HANDLE WINAPI CreateThread(void*, size_t, DWORD (WINAPI*)(void*), void*, DWORD, DWORD*);
__declspec(dllimport) DWORD WINAPI GetModuleFileNameA(HMODULE, char*, DWORD);
__declspec(dllimport) HMODULE WINAPI GetModuleHandleA(const char*);
__declspec(dllimport) HANDLE WINAPI CreateFileA(const char*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
__declspec(dllimport) BOOL WINAPI WriteFile(HANDLE, const void*, DWORD, DWORD*, void*);
__declspec(dllimport) BOOL WINAPI CloseHandle(HANDLE);
__declspec(dllimport) DWORD WINAPI GetTickCount(void);
__declspec(dllimport) void WINAPI Sleep(DWORD);
__declspec(dllimport) BOOL WINAPI VirtualProtect(void*, size_t, DWORD, DWORD*);
__declspec(dllimport) DWORD WINAPI GetCurrentThreadId(void);
__declspec(dllimport) DWORD WINAPI GetCurrentProcessId(void);
__declspec(dllimport) void WINAPI InitializeCriticalSection(CRITICAL_SECTION*);
__declspec(dllimport) void WINAPI EnterCriticalSection(CRITICAL_SECTION*);
__declspec(dllimport) void WINAPI LeaveCriticalSection(CRITICAL_SECTION*);
__declspec(dllimport) HANDLE WINAPI CreateEventA(void*, BOOL, BOOL, const char*);
__declspec(dllimport) BOOL WINAPI SetEvent(HANDLE);
__declspec(dllimport) DWORD WINAPI WaitForSingleObject(HANDLE, DWORD);
__declspec(dllimport) BOOL WINAPI IsBadReadPtr(const void*, size_t);
typedef struct { void *BaseAddress, *AllocationBase; DWORD AllocationProtect; size_t RegionSize; DWORD State, Protect, Type; } MBI_;
__declspec(dllimport) size_t WINAPI VirtualQuery(const void*, MBI_*, size_t);
/* Readable without touching the memory (IsBadReadPtr touches it and can trip guard pages: a peek
   crashed the game in 0.8.1-re). Committed, readable protection, no PAGE_GUARD/NOACCESS. */
static int mem_readable(uintptr_t a, uint32_t n) {
    while (n) {
        MBI_ m; if (!VirtualQuery((void *)a, &m, sizeof m)) return 0;
        if (m.State != 0x1000 /*MEM_COMMIT*/ || (m.Protect & 0x101) /*NOACCESS|GUARD*/ || !(m.Protect & 0xEE)) return 0;
        uintptr_t end = (uintptr_t)m.BaseAddress + m.RegionSize; uint32_t k = (uint32_t)(end - a); if (k >= n) return 1;
        a = end; n -= k;
    }
    return 1;
}
__declspec(dllimport) int __stdcall WSAStartup(unsigned short, WSADATA_*);
__declspec(dllimport) SOCKET __stdcall socket(int, int, int);
__declspec(dllimport) int __stdcall bind(SOCKET, const void*, int);
__declspec(dllimport) int __stdcall listen(SOCKET, int);
__declspec(dllimport) SOCKET __stdcall accept(SOCKET, void*, int*);
__declspec(dllimport) int __stdcall recv(SOCKET, char*, int, int);
__declspec(dllimport) int __stdcall send(SOCKET, const char*, int, int);
__declspec(dllimport) int __stdcall closesocket(SOCKET);
__declspec(dllimport) BOOL WINAPI PeekMessageA(MSG_*, void*, unsigned, unsigned, unsigned);
__declspec(dllimport) short WINAPI GetAsyncKeyState(int);
__declspec(dllimport) BOOL WINAPI MessageBeep(unsigned);
__declspec(dllimport) HMODULE WINAPI LoadLibraryA(const char*);
__declspec(dllimport) void *WINAPI AddVectoredExceptionHandler(unsigned long, long (WINAPI *)(void *));
__declspec(dllimport) HANDLE WINAPI OpenThread(DWORD, BOOL, DWORD);
__declspec(dllimport) DWORD WINAPI SuspendThread(HANDLE);
__declspec(dllimport) DWORD WINAPI ResumeThread(HANDLE);
__declspec(dllimport) BOOL WINAPI GetThreadContext(HANDLE, void *);
__declspec(dllimport) BOOL WINAPI SetThreadContext(HANDLE, void *);
typedef struct { DWORD attr; DWORD t[6]; DWORD szHi, szLo, r0, r1; char name[260]; char alt[14]; } FINDDATA;
__declspec(dllimport) HANDLE WINAPI FindFirstFileA(const char*, FINDDATA*);
__declspec(dllimport) BOOL WINAPI FindNextFileA(HANDLE, FINDDATA*);
__declspec(dllimport) BOOL WINAPI FindClose(HANDLE);
__declspec(dllimport) BOOL WINAPI ReadFile(HANDLE, void*, DWORD, DWORD*, void*);
__declspec(dllimport) BOOL WINAPI DeleteFileA(const char*);
__declspec(dllimport) BOOL WINAPI MoveFileA(const char*, const char*);
__declspec(dllimport) BOOL WINAPI CreateDirectoryA(const char*, void*);
__declspec(dllimport) void *WINAPI GetProcAddress(HMODULE, const char*);

/* ---------------- tiny libc ---------------- */
int _fltused = 0; /* MSVC ABI: marks float use; normally from the CRT */
static size_t s_len(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static void s_cpy(char *d, const char *s) { while ((*d++ = *s++)); }
static void s_cat(char *d, const char *s) { s_cpy(d + s_len(d), s); }
static int s_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
void *memset(void *d, int c, size_t n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { unsigned char *a = d; const unsigned char *b = s; while (n--) *a++ = *b++; return d; }
void *memmove(void *d, const void *s, size_t n) { unsigned char *a = d; const unsigned char *b = s; if (a < b) while (n--) *a++ = *b++; else { a += n; b += n; while (n--) *--a = *--b; } return d; }
static const char *s_str(const char *h, const char *n) {
    size_t k = s_len(n); for (; *h; h++) { size_t i = 0; while (i < k && h[i] == n[i]) i++; if (i == k) return h; } return 0;
}
static void u2s(char *d, uint32_t v) { char t[12]; int i = 0; do { t[i++] = '0' + v % 10; v /= 10; } while (v); while (i) *d++ = t[--i]; *d = 0; }
static void h2s(char *d, uint32_t v) { const char *x = "0123456789abcdef"; d[0]='0'; d[1]='x'; for (int i = 0; i < 8; i++) d[2+i] = x[(v >> (28 - 4*i)) & 15]; d[10] = 0; }
static void i2s(char *d, int32_t v) { if (v < 0) { *d++ = '-'; u2s(d, (uint32_t)(-(int64_t)v)); } else u2s(d, (uint32_t)v); }
static uint32_t s2u(const char *s) {
    uint32_t v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; for (;; s++) { char c = *s; if (c >= '0' && c <= '9') v = v*16 + c-'0'; else if (c >= 'a' && c <= 'f') v = v*16 + c-'a'+10; else if (c >= 'A' && c <= 'F') v = v*16 + c-'A'+10; else break; } return v; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v;
}

/* ---------------- game binding (build 1510) ---------------- */
#define G_APP          ((int **)0x0076C924)   /* CAppBase* */
#define APP_PAUSED     0x3C
#define APP_VM         0x9C
#define APP_GUI        0xB4
#define G_APPACTIVE    ((volatile int *)0x0075ECDC) /* 0 while the window is inactive (WM_ACTIVATE @0x005D9386): world freezes */
#define G_DEBUGPANEL   ((volatile int *)0x006F28A0) /* 0 in retail: panel compiled off */
#define IAT_LO 0x006E1000
#define IAT_HI (0x006E1000 + 0x350)
#define F_EXOSTR_CTOR  0x006534B0  /* CExoString::CExoString(const char*) thiscall */
#define F_EXOSTR_DTOR  0x005A1A50  /* CExoString::~CExoString() thiscall */
#define F_RUNSCRIPT    0x00697840  /* CVirtualMachine::RunScript(CExoString*, oid, int) thiscall */
#define F_LOADSCRIPT   0x00695600  /* CVirtualMachine::LoadScript(CExoString*) thiscall, 0 = ok */
#define F_SLOTCOPY     0x00694800  /* thiscall(vm, slot, code, len): copy code into a script slot */
static const uint32_t LOAD_CALLSITES[3] = { 0x00697872, 0x0069583C, 0x006974DE };
#define F_RETVAL       0x006948C0  /* CVirtualMachine::GetRunScriptReturnValue(int *type, int *value) thiscall */
/* 0.10.0: 12 bytes in this build: the ctor (0x006534B0) writes p, n and a third field (0xFFFFFFFF or a pool id).
   With only 8 bytes here the third write landed on whatever sat next on our stack (the saved ESI
   in vm_run: the combat crash) or next to a static. Kept at 16 for slack. */
typedef struct { char *p; uint32_t n; uint32_t pool; uint32_t pad; } CExoString;
typedef void (__attribute__((thiscall)) *ctor_t)(CExoString *, const char *);
typedef void (__attribute__((thiscall)) *dtor_t)(CExoString *);
typedef int  (__attribute__((thiscall)) *run_t)(void *, CExoString *, uint32_t, int);
/* 0.10.0: calls into the game go through these. Crash in combat sync (copy B): after a script ran,
   ESI held 0xffffffff although the convention says the callee keeps EBX/ESI/EDI/EBP; the mod's code
   used the trashed register (read 0xffffffff; earlier runs most likely wrote through it: our combat
   flag and enemy table were zeroed between frames). Each wrapper saves and restores all four. */
volatile int32_t g_gcDelta; volatile uint32_t g_gcDeltaFn; volatile int g_gcBad;
static __attribute__((noinline)) uint32_t gcall(uint32_t fn, uint32_t ecx, int n, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t rc; int32_t d;
    /* EBX keeps ESP from before the argument pushes, so a callee that pops the wrong number of
       bytes (cdecl vs thiscall mix-up) can't shift our saved registers. d = bytes it got wrong. */
    __asm__ volatile(
        "pushl %%ebx\n\tpushl %%esi\n\tpushl %%edi\n\tpushl %%ebp\n\tmovl %%esp, %%ebx\n\t"
        "cmpl $3, %[n]\n\tjb 1f\n\tpushl %[c]\n"
        "1:\n\tcmpl $2, %[n]\n\tjb 2f\n\tpushl %[b]\n"
        "2:\n\tcmpl $1, %[n]\n\tjb 3f\n\tpushl %[a]\n"
        "3:\n\tmovl %[t], %%ecx\n\tcalll *%[f]\n\t"
        "movl %%ebx, %%edx\n\tsubl %%esp, %%edx\n\tmovl %%ebx, %%esp\n\t"
        "popl %%ebp\n\tpopl %%edi\n\tpopl %%esi\n\tpopl %%ebx\n\t"
        : "=a"(rc), "=d"(d)
        : [f] "m"(fn), [t] "m"(ecx), [n] "m"(n), [a] "m"(a), [b] "m"(b), [c] "m"(c)
        : "ecx", "memory", "cc");
    if (d) { g_gcDelta = d; g_gcDeltaFn = fn; g_gcBad++; }
    return rc;
}
typedef int  (__attribute__((thiscall)) *ret_t)(void *, int *, int *);
typedef int  (__attribute__((thiscall)) *load_t)(void *, CExoString *);
typedef void (__attribute__((thiscall)) *copy_t)(void *, void *, const void *, uint32_t);

#ifdef JE_RE
/* ---------------- re/target: animation request log ----------------
   FUN_00443d80(ctrl, req[16 dwords], flag) is the engine's "play this animation" entry (PlayCombatAnimation
   #940 and ~25 game callers use it); ctrl[0] = creature. RE build only: a 6-byte entry patch logs every
   request of creatures that are in combat (+0x588 != 0) into a ring the client reads with je.animLog. */
typedef struct { uint32_t tick, creature, req[16], flag; } AnimRec;
static AnimRec g_anim[128]; static volatile uint32_t g_animN; static int g_animOn;
__attribute__((used)) uint32_t g_ret443 = 0x00443d86;
__attribute__((used)) void __cdecl anim_log(uint32_t ctrl, uint32_t req, uint32_t flag) {
    if (!g_animOn || !ctrl || !req) return; uint32_t c = *(uint32_t *)(uintptr_t)ctrl;
    if (!c || !*(uint32_t *)(uintptr_t)(c + 0x588)) return;
    AnimRec *a = &g_anim[g_animN % 128]; a->tick = GetTickCount(); a->creature = c; a->flag = flag;
    for (int i = 0; i < 16; i++) a->req[i] = ((uint32_t *)(uintptr_t)req)[i]; g_animN++;
}
__asm__(".text\n.globl _hk443d80\n_hk443d80:\n"
        "pushal\n\tpushl 40(%esp)\n\tpushl 40(%esp)\n\tpushl %ecx\n\tcall _anim_log\n\taddl $12, %esp\n\tpopal\n\t"
        "pushl %ebx\n\tpushl %ebp\n\tmovl %ecx, %ebx\n\tmovl (%ebx), %eax\n\tjmp *_g_ret443\n");
extern void hk443d80(void);
static int anim_hook(void) {
    uint8_t *p = (uint8_t *)0x00443d80; static const uint8_t want[6] = { 0x53, 0x55, 0x8b, 0xd9, 0x8b, 0x03 };
    for (int i = 0; i < 6; i++) if (p[i] != want[i]) return 0;
    DWORD old; VirtualProtect(p, 6, 0x40, &old); p[0] = 0xE9; *(int32_t *)(p + 1) = (int32_t)((uint32_t)(uintptr_t)hk443d80 - (0x00443d80 + 5)); p[5] = 0x90;
    VirtualProtect(p, 6, old, &old); return 1;
}

#endif
/* ---------------- in-memory scripts ---------------- */
#define MS_MAX 128   /* 0.8.0: was 32; long test sessions filled it ("script table full") */
#define MS_POOL 262144
typedef struct { char name[17]; uint32_t off, len, cap; } MemScript;
static MemScript g_ms[MS_MAX]; static int g_msn; static uint8_t g_pool[MS_POOL]; static uint32_t g_poolUsed;
static volatile uint32_t g_memLoads;
static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int name_eq(const char *a, const char *b) { while (*a && lc(*a) == lc(*b)) { a++; b++; } return !*a && !*b; }
static MemScript *ms_find(const char *n) { for (int i = 0; i < g_msn; i++) if (name_eq(g_ms[i].name, n)) return &g_ms[i]; return 0; }
/* Replacement for the call to LoadScript at the patched call sites. Same contract. */
static int __attribute__((thiscall)) hkLoadScript(void *vm, CExoString *resref) {
    MemScript *m = (resref && resref->p) ? ms_find(resref->p) : 0;
    if (!m) return (int)gcall(F_LOADSCRIPT, (uint32_t)(uintptr_t)vm, 1, (uint32_t)(uintptr_t)resref, 0, 0);
    int *depth = (int *)((char *)vm + 0xC);
    if (*depth + 1 > 7) return -0x5E;           /* same as the original: too deep */
    *depth += 1;
    const uint8_t *ncs = g_pool + m->off;       /* stored with its 13-byte NCS header */
    ((copy_t)F_SLOTCOPY)(vm, (char *)vm + 0x10 + *depth * 0x28, ncs + 13, m->len - 13);
    g_memLoads++;
    return 0;
}
static int patch_callsites(void) {
    for (int i = 0; i < 3; i++) {
        uint8_t *p = (uint8_t *)LOAD_CALLSITES[i];
        if (p[0] != 0xE8 || (uint32_t)(LOAD_CALLSITES[i] + 5 + *(int32_t *)(p + 1)) != F_LOADSCRIPT) return 0;
    }
    for (int i = 0; i < 3; i++) {
        uint8_t *p = (uint8_t *)LOAD_CALLSITES[i]; DWORD old;
        VirtualProtect(p, 5, 0x40, &old);
        *(int32_t *)(p + 1) = (int32_t)((uint32_t)(uintptr_t)hkLoadScript - (LOAD_CALLSITES[i] + 5));
        VirtualProtect(p, 5, old, &old);
    }
    return 1;
}
static int hexval(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

/* ---------------- state ---------------- */
static char g_dir[260];
static CRITICAL_SECTION g_cs;
static volatile uint32_t g_frames, g_lastFrameTick, g_mainTid;
static volatile int g_hooked;
static volatile int g_keepActive;
static BOOL (WINAPI *g_realPeek)(MSG_*, void*, unsigned, unsigned, unsigned);
static uint16_t g_port;

typedef struct { char method[48]; char resref[160]; const char *hex; int hexLen; char idtok[32]; uint32_t oid; int ival; int hasIval;
                 uint32_t cf[5]; char out[4096]; volatile int done; HANDLE ev; DWORD deadline; volatile int abandoned; } Req;
#define QMAX 32
static Req *g_q[QMAX]; static int g_qn;

static void logline(const char *s) {
    char path[300]; s_cpy(path, g_dir); s_cat(path, "jebridge.log");
    /* 0.10.0: share read+write (a reader made lines get lost), one retry */
    HANDLE h = CreateFileA(path, 4 /*FILE_APPEND_DATA*/, 3, 0, 4 /*OPEN_ALWAYS*/, 0x80, 0);
    if (h == (HANDLE)-1) { Sleep(5); h = CreateFileA(path, 4, 3, 0, 4, 0x80, 0); }
    if (h == (HANDLE)-1) return; DWORD w; WriteFile(h, s, (DWORD)s_len(s), &w, 0); WriteFile(h, "\r\n", 2, &w, 0); CloseHandle(h);
}

/* Refuse to touch the game unless the IAT region holds pointers and the debug flag sits
   where build 1510 has it. Cheap guard against running on another build. */
static int check_game(void) {
    if (IsBadReadPtr((void *)IAT_LO, IAT_HI - IAT_LO)) return 0;
    if (IsBadReadPtr((void *)G_DEBUGPANEL, 4) || IsBadReadPtr((void *)F_RUNSCRIPT, 16)) return 0;
    /* RunScript prologue bytes are checked at hook time against the dump (55 8b ec / 83 ec / 6a ff) loosely */
    return 1;
}

/* ---------------- crash breadcrumbs ---------------- */
/* g_stage names what the bridge is doing on the main thread; a vectored handler logs access
   violations (first few only) with the faulting address and the current stage. */
static volatile const char *g_stage = "idle";
static volatile int g_avLogged, g_watchLogged;
static volatile uint32_t g_watchAddr; static uint32_t g_wsEip[16], g_wsCnt[16]; static int g_wseen;
static volatile int g_inFrame, g_reenter;
static uint32_t g_pin[4][2]; static int g_npin;
static uint32_t cb_watch_addr(int i);   /* combat.inc */
static HMODULE g_self;
static long WINAPI veh(void *ep) {
    uint32_t **pp = (uint32_t **)ep; uint32_t *rec = pp[0]; uint32_t *ctx = pp[1];
    if (rec[0] == 0x80000004 && (ctx[0x14 / 4] & 1) && g_watchAddr) {   /* re/target: who writes this address; each eip once */
        uint32_t e = rec[3]; int k = 0; while (k < g_wseen && g_wsEip[k] != e) k++;
        if (k < g_wseen) g_wsCnt[k]++;
        else if (g_wseen < 16) { g_wsEip[g_wseen] = e; g_wsCnt[g_wseen++] = 1; char m[300], a[16]; uint32_t sp = ctx[0xC4 / 4];
            s_cpy(m, "WATCH2: write to "); h2s(a, g_watchAddr); s_cat(m, a); s_cat(m, " by eip "); h2s(a, e); s_cat(m, a);
            s_cat(m, " value "); h2s(a, *(volatile uint32_t *)(uintptr_t)g_watchAddr); s_cat(m, a); s_cat(m, " ebp "); h2s(a, ctx[0xB4 / 4]); s_cat(m, a); s_cat(m, " stack");
            int got = 0; for (int i = 0; i < 96 && got < 14 && mem_readable(sp + i * 4, 4); i++) { uint32_t v = *(uint32_t *)(uintptr_t)(sp + i * 4);
                if (v >= 0x00401000 && v < 0x006c0000) { s_cat(m, " "); h2s(a, v); s_cat(m, a); got++; } }   /* code addresses only: likely return addresses */
            uint32_t bp = ctx[0xB4 / 4]; s_cat(m, " frames");
            for (int i = 0; i < 6 && mem_readable(bp, 8); i++) { s_cat(m, " "); h2s(a, *(uint32_t *)(uintptr_t)(bp + 4)); s_cat(m, a); bp = *(uint32_t *)(uintptr_t)bp; }
            logline(m); }
        ctx[0x14 / 4] = 0; return -1;
    }
    if (rec[0] == 0x80000004 && (ctx[0x14 / 4] & 3)) {        /* 0.10.0 debug: hardware write watch hit */
        if (g_watchLogged < 80) { g_watchLogged++;
            char m[200], a[16]; s_cpy(m, "WATCH: write to "); s_cat(m, (ctx[0x14 / 4] & 1) ? "g_cbInFight" : "g_cbH[0].used");
            s_cat(m, " by eip "); h2s(a, rec[3]); s_cat(m, a); s_cat(m, " (dll base "); h2s(a, (uint32_t)(uintptr_t)g_self); s_cat(m, a);
            s_cat(m, ") value "); { uint32_t wa = cb_watch_addr((ctx[0x14 / 4] & 1) ? 0 : 1); h2s(a, *(volatile uint32_t *)(uintptr_t)wa); } s_cat(m, a);
            s_cat(m, " esp "); h2s(a, ctx[0xC4 / 4]); s_cat(m, a);
            s_cat(m, " ret? "); h2s(a, mem_readable(ctx[0xC4 / 4], 4) ? *(uint32_t *)(uintptr_t)ctx[0xC4 / 4] : 0); s_cat(m, a);
            s_cat(m, " stage "); s_cat(m, (const char *)g_stage); logline(m); }
        ctx[0x14 / 4] = 0; return -1;                          /* EXCEPTION_CONTINUE_EXECUTION */
    }
    if (rec[0] != 0xC0000005 || g_avLogged >= 5) return 0;   /* EXCEPTION_CONTINUE_SEARCH */
    g_avLogged++;
    char m[200], a[16];
    s_cpy(m, "CRASH? access violation at eip "); h2s(a, rec[3]); s_cat(m, a);
    s_cat(m, " (dll base "); h2s(a, (uint32_t)(uintptr_t)g_self); s_cat(m, a);
    s_cat(m, ") rw "); u2s(a, rec[5]); s_cat(m, a); s_cat(m, " addr "); h2s(a, rec[6]); s_cat(m, a);
    s_cat(m, " esp "); h2s(a, ctx[0xC4 / 4]); s_cat(m, a);
    s_cat(m, " stage "); s_cat(m, (const char *)g_stage);
    logline(m);
    { static const char *rn[] = {"edi","esi","ebx","edx","ecx","eax","ebp"}; char r[400]; s_cpy(r, "  regs");
      for (int i = 0; i < 7; i++) { s_cat(r, " "); s_cat(r, rn[i]); s_cat(r, "="); h2s(a, ctx[0x9C / 4 + i]); s_cat(r, a); }
      s_cat(r, " reenter="); u2s(a, (uint32_t)g_reenter); s_cat(r, a); logline(r);
      uint32_t sp = ctx[0xC4 / 4]; s_cpy(r, "  stack");
      for (int i = 0; i < 16 && mem_readable(sp + i * 4, 4); i++) { s_cat(r, " "); h2s(a, *(uint32_t *)(uintptr_t)(sp + i * 4)); s_cat(r, a); }
      logline(r); }
    return 0;
}

#include "netsync.inc"

/* ---------------- main-thread work ---------------- */
static void run_on_main(Req *r) {
    int *app = *G_APP;
    if (r->method[0] != 'j' || r->method[1] != 'e' || r->method[2] != '.') { s_cpy(r->out, "{\"error\":{\"code\":-32601,\"message\":\"method not found\"}}"); return; }
    if (!app) { s_cpy(r->out, "{\"error\":{\"code\":-32010,\"message\":\"app object not ready\"}}"); return; }
    char a[16], b[16], c[16];
    if (s_eq(r->method, "je.state")) {
        h2s(a, (uint32_t)(uintptr_t)app); u2s(b, (uint32_t)*(int *)((char *)app + APP_PAUSED)); h2s(c, (uint32_t)*(int *)((char *)app + APP_VM));
        s_cpy(r->out, "{\"result\":{\"app\":\""); s_cat(r->out, a); s_cat(r->out, "\",\"paused\":"); s_cat(r->out, b);
        s_cat(r->out, ",\"vm\":\""); s_cat(r->out, c); s_cat(r->out, "\",\"appActive\":"); u2s(c, (uint32_t)*G_APPACTIVE); s_cat(r->out, c); s_cat(r->out, ",\"keepActive\":"); u2s(c, (uint32_t)g_keepActive); s_cat(r->out, c);
        s_cat(r->out, ",\"debugPanelFlag\":"); u2s(a, (uint32_t)*G_DEBUGPANEL); s_cat(r->out, a);
        s_cat(r->out, ",\"thread\":"); u2s(a, GetCurrentThreadId()); s_cat(r->out, a); s_cat(r->out, ",\"mainThread\":"); u2s(a, g_mainTid); s_cat(r->out, a); s_cat(r->out, "}}");
        return;
    }
    if (s_eq(r->method, "je.netStatus")) { net_status(r->out); return; }
    if (s_eq(r->method, "je.evalMany")) {
        /* params: names "a,b,c" (comma separated, each <=16 chars, max 8). Runs them back to back
           in this one main-thread call and returns every value (null when a script gave no int). */
        if (!r->resref[0]) { s_cpy(r->out, "{\"error\":{\"code\":-32602,\"message\":\"je.evalMany requires 'names'\"}}"); return; }
        s_cpy(r->out, "{\"result\":{\"values\":[");
        const char *p = r->resref; int first = 1, count = 0;
        while (*p && count < 8) {
            char nm[17]; int i = 0; while (*p && *p != ',' && i < 16) nm[i++] = *p++; nm[i] = 0; if (*p == ',') p++;
            int v = 0; int ok = vm_eval(nm, &v);
            if (!first) s_cat(r->out, ","); first = 0;
            if (ok) { i2s(a, v); s_cat(r->out, a); } else s_cat(r->out, "null");
            count++;
        }
        s_cat(r->out, "]}}"); return;
    }
    if (s_eq(r->method, "je.peek")) {
        /* RE helper: read up to 512 bytes of game memory. params: oid = address, value = length */
        uint32_t addr = r->oid, n = r->hasIval ? (uint32_t)r->ival : 64; if (n > 512) n = 512;
        if (!addr || !mem_readable(addr, n)) { s_cpy(r->out, "{\"error\":{\"code\":-32004,\"message\":\"unreadable\"}}"); return; }
        s_cpy(r->out, "{\"result\":{\"hex\":\""); char *o = r->out + s_len(r->out); const char *x = "0123456789abcdef";
        for (uint32_t i = 0; i < n; i++) { uint8_t b = ((uint8_t *)(uintptr_t)addr)[i]; *o++ = x[b >> 4]; *o++ = x[b & 15]; }
        *o = 0; s_cat(r->out, "\"}}"); return;
    }
    if (s_eq(r->method, "je.playerPtr")) {
        /* RE helper: the player creature as GetPlayer finds it (app+0xA0 -> FUN_00424700) and, by its id,
           the object GetFacing uses (FUN_004610b0) and the vector GetFacing reads (FUN_00412410). */
        int *app = *G_APP; void *srv = app ? *(void **)((char *)app + 0xA0) : 0;
        uint32_t pc = srv ? gcall(0x00424700, (uint32_t)(uintptr_t)srv, 0, 0, 0, 0) : 0, id = 0, obj = 0, vec = 0;
        if (pc && mem_readable(pc, 12)) {
            id = *(uint32_t *)(uintptr_t)(pc + 8);
            uint32_t idc = id, o = 0; char ok = (char)gcall(0x004610b0, (uint32_t)(uintptr_t)srv, 2, (uint32_t)(uintptr_t)&idc, (uint32_t)(uintptr_t)&o, 0);
            if (ok == *(char *)0x006e8470 && o) { obj = o; vec = gcall(0x00412410, o, 0, 0, 0, 0); }
        }
        char a1[12]; s_cpy(r->out, "{\"result\":{\"creature\":"); u2s(a1, pc); s_cat(r->out, a1);
        s_cat(r->out, ",\"id\":"); u2s(a1, id); s_cat(r->out, a1); s_cat(r->out, ",\"obj\":"); u2s(a1, obj); s_cat(r->out, a1);
        s_cat(r->out, ",\"vec\":"); u2s(a1, vec); s_cat(r->out, a1); s_cat(r->out, "}}"); return;
    }
#ifdef JE_RE
    if (s_eq(r->method, "je.creature")) {
        /* RE helper (re/target): creature pointer for an object id (FUN_004611b0, thiscall on app+0xA0) */
        int *app = *G_APP; void *srv = app ? *(void **)((char *)app + 0xA0) : 0; uint32_t id = r->oid;
        uint32_t c = srv ? gcall(0x004611b0, (uint32_t)(uintptr_t)srv, 1, (uint32_t)(uintptr_t)&id, 0, 0) : 0;
        char a1[12]; s_cpy(r->out, "{\"result\":{\"creature\":"); u2s(a1, c); s_cat(r->out, a1); s_cat(r->out, "}}"); return;
    }
    if (s_eq(r->method, "je.call")) {
        /* RE helper (re/target, test copies only): call a game function, thiscall, ecx + up to 2 stack args.
           Only addresses inside the game's code section. */
        uint32_t fn = r->cf[0];
        if (fn < 0x00401000 || fn >= 0x006c0000 || r->cf[2] > 2) { s_cpy(r->out, "{\"error\":{\"code\":-32602,\"message\":\"bad fn or n\"}}"); return; }
        uint32_t rc = gcall(fn, r->cf[1], (int)r->cf[2], r->cf[3], r->cf[4], 0);
        char a1[12]; s_cpy(r->out, "{\"result\":{\"eax\":"); u2s(a1, rc); s_cat(r->out, a1); s_cat(r->out, "}}"); return;
    }
    if (s_eq(r->method, "je.animLog")) {
        /* re/target: value 1 = hook + start, 0 = stop; returns the newest records (oid = how many, max 12) */
        if (r->hasIval) { static int hooked; if (!hooked) hooked = anim_hook(); g_animOn = r->ival && hooked; if (r->ival) g_animN = 0;
            s_cpy(r->out, g_animOn ? "{\"result\":{\"on\":true}}" : "{\"result\":{\"on\":false}}"); return; }
        uint32_t want = r->oid == 0x7F000000 ? 12 : r->oid; if (want > 12) want = 12; uint32_t n = g_animN, k0 = n > want ? n - want : 0;
        char a1[12]; s_cpy(r->out, "{\"result\":{\"n\":"); u2s(a1, n); s_cat(r->out, a1); s_cat(r->out, ",\"recs\":[");
        for (uint32_t k = k0; k < n; k++) { AnimRec *a = &g_anim[k % 128]; if (k > k0) s_cat(r->out, ",");
            s_cat(r->out, "["); u2s(a1, a->tick); s_cat(r->out, a1); s_cat(r->out, ","); u2s(a1, a->creature); s_cat(r->out, a1); s_cat(r->out, ","); u2s(a1, a->flag); s_cat(r->out, a1);
            for (int i = 0; i < 16; i++) { s_cat(r->out, ","); u2s(a1, a->req[i]); s_cat(r->out, a1); } s_cat(r->out, "]"); }
        s_cat(r->out, "]}}"); return;
    }
    if (s_eq(r->method, "je.animPlay")) {
        /* re/target: FUN_00443d80(*(creature+0x358), req, 0) with a 64-byte request given as hex; oid = creature */
        static uint32_t req[16]; uintptr_t c = r->oid;
        if (!c || !mem_readable(c + 0x358, 4) || !r->hex || r->hexLen != 128) { s_cpy(r->out, "{\"error\":{\"code\":-32602,\"message\":\"bad creature or hex\"}}"); return; }
        for (int i = 0; i < 64; i++) { int hi = hexval(r->hex[2*i]), lo = hexval(r->hex[2*i+1]); ((uint8_t *)req)[i] = (uint8_t)(hi * 16 + lo); }
        uint32_t ctrl = *(uint32_t *)(c + 0x358); if (!ctrl) { s_cpy(r->out, "{\"error\":{\"code\":-32004,\"message\":\"no controller\"}}"); return; }
        uint32_t rc = gcall(0x00443d80, ctrl, 2, (uint32_t)(uintptr_t)req, 0, 0);
        char a1[12]; s_cpy(r->out, "{\"result\":{\"eax\":"); u2s(a1, rc); s_cat(r->out, a1); s_cat(r->out, "}}"); return;
    }
    if (s_eq(r->method, "je.pin")) {
        /* TEST HOOK (re/target, test copies only): write a dword every frame. oid = address (0 = clear all),
           value = dword. Up to 4 pins. */
        if (!r->oid) { g_npin = 0; s_cpy(r->out, "{\"result\":{\"pins\":0}}"); return; }
        if (!mem_readable(r->oid, 4) || g_npin >= 4) { s_cpy(r->out, "{\"error\":{\"code\":-32004,\"message\":\"bad address or full\"}}"); return; }
        g_pin[g_npin][0] = r->oid; g_pin[g_npin][1] = (uint32_t)r->ival; g_npin++;
        *(volatile uint32_t *)(uintptr_t)r->oid = (uint32_t)r->ival;
        char a1[12]; s_cpy(r->out, "{\"result\":{\"pins\":"); u2s(a1, (uint32_t)g_npin); s_cat(r->out, a1); s_cat(r->out, "}}"); return;
    }
#endif
    if (s_eq(r->method, "je.testSetDir")) {
        /* TEST HOOK (test copies): turn the local player by writing its orientation vector (creature+0x50),
           oid = cos*10000 + 20000, value = sin*10000 + 20000. Scripts cannot turn the player (SetFacing). */
        int *app = *G_APP; void *srv = app ? *(void **)((char *)app + 0xA0) : 0;
        uintptr_t pc = srv ? gcall(0x00424700, (uint32_t)(uintptr_t)srv, 0, 0, 0, 0) : 0;
        if (!pc || !r->hasIval || !mem_readable(pc + 0x50, 8)) { s_cpy(r->out, "{\"error\":{\"code\":-32004,\"message\":\"no player\"}}"); return; }
        *(float *)(pc + 0x50) = ((int)r->oid - 20000) / 10000.0f; *(float *)(pc + 0x54) = (r->ival - 20000) / 10000.0f;
        s_cpy(r->out, "{\"result\":{\"ok\":true}}"); return;
    }
    if (s_eq(r->method, "je.netTune")) {
        if (!r->hasIval) { s_cpy(r->out, "{\"error\":{\"code\":-32602,\"message\":\"je.netTune requires int param 'value'\"}}"); return; }
        g_tune = r->ival; g_haveOrder = 0; net_status(r->out); return;
    }
    if (s_eq(r->method, "je.netStart")) { g_netArmed = 1; logline("net: armed by je.netStart"); net_status(r->out); return; }
    if (s_eq(r->method, "je.netPause")) {
        if (!r->hasIval) { s_cpy(r->out, "{\"error\":{\"code\":-32602,\"message\":\"je.netPause requires int param 'value'\"}}"); return; }
        g_netPaused = r->ival != 0; net_status(r->out); return;
    }
    if (s_eq(r->method, "je.keepActive")) {
        if (!r->hasIval) { s_cpy(r->out, "{\"error\":{\"code\":-32602,\"message\":\"je.keepActive requires int param 'value'\"}}"); return; }
        g_keepActive = r->ival != 0; if (g_keepActive) *G_APPACTIVE = 1;
        s_cpy(r->out, "{\"result\":{\"ok\":true,\"keepActive\":"); u2s(a, (uint32_t)g_keepActive); s_cat(r->out, a);
        s_cat(r->out, ",\"appActive\":"); u2s(a, (uint32_t)*G_APPACTIVE); s_cat(r->out, a); s_cat(r->out, "}}"); return;
    }
    if (s_eq(r->method, "je.setDebugPanel")) {
        if (!r->hasIval) { s_cpy(r->out, "{\"error\":{\"code\":-32602,\"message\":\"je.setDebugPanel requires int param 'value'\"}}"); return; }
        DWORD old; VirtualProtect((void *)G_DEBUGPANEL, 4, 0x04, &old); *G_DEBUGPANEL = r->ival; VirtualProtect((void *)G_DEBUGPANEL, 4, old, &old);
        s_cpy(r->out, "{\"result\":{\"ok\":true,\"debugPanelFlag\":"); u2s(a, (uint32_t)*G_DEBUGPANEL); s_cat(r->out, a); s_cat(r->out, "}}");
        return;
    }
    if (s_eq(r->method, "je.loadScript")) {
        /* params: name (resref, max 16 chars), hex (full NCS file as hex) */
        if (!r->resref[0] || s_len(r->resref) > 16 || !r->hex || r->hexLen < 2 * 15 || (r->hexLen & 1)) {
            s_cpy(r->out, "{\"error\":{\"code\":-32602,\"message\":\"je.loadScript requires 'name' (<=16 chars) and 'hex' (an NCS file)\"}}"); return; }
        uint32_t n = (uint32_t)r->hexLen / 2;
        MemScript *m = ms_find(r->resref);
        if (!m) { if (g_msn >= MS_MAX) { s_cpy(r->out, "{\"error\":{\"code\":-32003,\"message\":\"script table full\"}}"); return; }
                  m = &g_ms[g_msn++]; s_cpy(m->name, r->resref); m->len = 0; }
        if (g_poolUsed + n > MS_POOL) { s_cpy(r->out, "{\"error\":{\"code\":-32003,\"message\":\"script pool full\"}}"); return; }
        uint8_t *dst = g_pool + g_poolUsed;
        for (uint32_t i = 0; i < n; i++) { int hi = hexval(r->hex[2*i]), lo = hexval(r->hex[2*i+1]);
            if (hi < 0 || lo < 0) { s_cpy(r->out, "{\"error\":{\"code\":-32602,\"message\":\"bad hex\"}}"); return; } dst[i] = (uint8_t)(hi * 16 + lo); }
        if (dst[0] != 'N' || dst[1] != 'C' || dst[2] != 'S' || dst[8] != 'B' ||
            (uint32_t)((dst[9] << 24) | (dst[10] << 16) | (dst[11] << 8) | dst[12]) != n) {
            s_cpy(r->out, "{\"error\":{\"code\":-32602,\"message\":\"not an NCS V1.0 file or size mismatch\"}}"); return; }
        if (m->cap >= n && m->len) { memmove(g_pool + m->off, dst, n); m->len = n; }
        else { m->off = g_poolUsed; m->len = n; m->cap = n; g_poolUsed += n; }
        s_cpy(r->out, "{\"result\":{\"ok\":true,\"bytes\":"); u2s(a, n); s_cat(r->out, a);
        s_cat(r->out, ",\"memLoads\":"); u2s(a, g_memLoads); s_cat(r->out, a); s_cat(r->out, "}}"); return;
    }
    if (s_eq(r->method, "je.runScript") || s_eq(r->method, "je.evalScript")) {
        if (!r->resref[0]) { s_cpy(r->out, "{\"error\":{\"code\":-32602,\"message\":\"je.runScript requires string param 'resref'\"}}"); return; }
        void *vm = *(void **)((char *)app + APP_VM);
        if (!vm) { s_cpy(r->out, "{\"error\":{\"code\":-32010,\"message\":\"script VM not ready\"}}"); return; }
        CExoString s; s.p = 0; s.n = 0;
        gcall(F_EXOSTR_CTOR, (uint32_t)(uintptr_t)&s, 1, (uint32_t)(uintptr_t)r->resref, 0, 0);
        int rc = (int)gcall(F_RUNSCRIPT, (uint32_t)(uintptr_t)vm, 3, (uint32_t)(uintptr_t)&s, r->oid, 1);
        gcall(F_EXOSTR_DTOR, (uint32_t)(uintptr_t)&s, 0, 0, 0, 0);
        if (s_eq(r->method, "je.evalScript")) {
            int typ = 0, val = 0, got = rc == 1 ? (int)gcall(F_RETVAL, (uint32_t)(uintptr_t)vm, 2, (uint32_t)(uintptr_t)&typ, (uint32_t)(uintptr_t)&val, 0) : 0;
            if (!got || typ != 3) {
                s_cpy(r->out, "{\"error\":{\"code\":-32011,\"message\":\"script did not return an int\",\"data\":{\"rc\":"); i2s(a, rc); s_cat(r->out, a);
                s_cat(r->out, ",\"type\":"); i2s(a, typ); s_cat(r->out, a); s_cat(r->out, "}}}"); return;
            }
            s_cpy(r->out, "{\"result\":{\"value\":"); i2s(a, val); s_cat(r->out, a);
            s_cat(r->out, ",\"thread\":"); u2s(a, GetCurrentThreadId()); s_cat(r->out, a); s_cat(r->out, "}}");
            return;
        }
        s_cpy(r->out, "{\"result\":{\"ok\":"); s_cat(r->out, rc ? "true" : "false"); s_cat(r->out, ",\"rc\":"); i2s(a, rc); s_cat(r->out, a);
        s_cat(r->out, ",\"thread\":"); u2s(a, GetCurrentThreadId()); s_cat(r->out, a); s_cat(r->out, "}}");
        return;
    }
    s_cpy(r->out, "{\"error\":{\"code\":-32601,\"message\":\"method not found\"}}");
}

static void pump(void) {
    int started = 0;
    for (;;) {
        Req *r = 0;
        EnterCriticalSection(&g_cs);
        if (g_qn && started < 16) { r = g_q[0]; for (int i = 1; i < g_qn; i++) g_q[i-1] = g_q[i]; g_qn--; }
        LeaveCriticalSection(&g_cs);
        if (!r) break;
        started++;
        if (r->abandoned || GetTickCount() > r->deadline) { r->done = 2; SetEvent(r->ev); continue; }
        run_on_main(r); r->done = 1; SetEvent(r->ev);
    }
}


static BOOL WINAPI hkPeek(MSG_ *m, void *h, unsigned a, unsigned b, unsigned f) {
    if (!g_mainTid) g_mainTid = GetCurrentThreadId();
    if (GetCurrentThreadId() == g_mainTid) {
        uint32_t t = GetTickCount();
        if (t != g_lastFrameTick) { g_frames++; g_lastFrameTick = t; }
        if (g_keepActive) *G_APPACTIVE = 1;
        /* 0.10.0: if the game pumps messages while one of our scripts runs, don't re-enter */
        if (g_inFrame) g_reenter++;
        else { g_inFrame = 1; pump(); net_tick(); for (int i = 0; i < g_npin; i++) if (mem_readable(g_pin[i][0], 4)) *(volatile uint32_t *)(uintptr_t)g_pin[i][0] = g_pin[i][1]; g_inFrame = 0; }
    }
    return g_realPeek(m, h, a, b, f);
}

static int hook_iat(void) {
    void *target = (void *)PeekMessageA;
    for (uintptr_t p = IAT_LO; p < IAT_HI; p += 4) {
        if (*(void **)p == target) {
            DWORD old; VirtualProtect((void *)p, 4, 0x04, &old);
            g_realPeek = target; *(void **)p = (void *)hkPeek;
            VirtualProtect((void *)p, 4, old, &old);
            return 1;
        }
    }
    return 0;
}

/* ---------------- transport ---------------- */
static void jstr_field(const char *line, const char *key, char *out, int max) {
    out[0] = 0; char k[48]; s_cpy(k, "\""); s_cat(k, key); s_cat(k, "\"");
    const char *p = s_str(line, k); if (!p) return; p += s_len(k);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return; p++;
    int i = 0; while (*p && *p != '"' && i < max - 1) out[i++] = *p++; out[i] = 0;
}
static int jnum_field(const char *line, const char *key, uint32_t *v) {
    char k[48]; s_cpy(k, "\""); s_cat(k, key); s_cat(k, "\"");
    const char *p = s_str(line, k); if (!p) return 0; p += s_len(k);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '"') p++;
    if (!((*p >= '0' && *p <= '9'))) return 0;
    *v = s2u(p); return 1;
}
static void id_token(const char *line, char *out) {
    const char *p = s_str(line, "\"id\""); s_cpy(out, "null"); if (!p) return; p += 4;
    while (*p == ' ' || *p == ':') p++;
    int i = 0; if (*p == '"') { out[i++] = *p++; while (*p && *p != '"' && i < 28) out[i++] = *p++; out[i++] = '"'; }
    else while (*p && *p != ',' && *p != '}' && *p != ' ' && i < 30) out[i++] = *p++;
    out[i] = 0;
}

/* A sink is either a socket (s != 0) or a buffer (fileOut). */
static char *g_fileOut; /* set by the file transport thread only (single thread) */
static void reply(SOCKET s, const char *idtok, const char *body /* {"result":..} or {"error":..} */) {
    /* 0.8.1: was char buf[700] - je.netStatus with the Steam block (~950 chars since 0.7.0) and a 512-byte
       je.peek (1,050 chars) overflowed it and smashed the bridge thread's stack (the peek crashed the game).
       Req.out is 2048, so 2048 + the id fits; longer bodies are cut, never overflowed. */
    char buf[4300]; s_cpy(buf, "{\"id\":"); s_cat(buf, idtok); s_cat(buf, ",");
    { uint32_t room = sizeof buf - s_len(buf) - 2, n = s_len(body + 1); if (n > room) n = room; char *d = buf + s_len(buf); memcpy(d, body + 1, n); d[n] = 0; }
    s_cat(buf, "\n");
    if (s) send(s, buf, (int)s_len(buf), 0); else if (g_fileOut) s_cat(g_fileOut, buf);
}

static void handle_line(SOCKET s, char *line) {
    char method[48], idtok[32], a[16];
    id_token(line, idtok); jstr_field(line, "method", method, sizeof method);
    if (!method[0]) { reply(s, "null", "{\"error\":{\"code\":-32700,\"message\":\"parse error\"}}"); return; }
    if (s_eq(method, "ping")) {
        char out[400]; uint32_t age = g_lastFrameTick ? GetTickCount() - g_lastFrameTick : 0xFFFFFFFF;
        s_cpy(out, "{\"result\":{\"pong\":true,\"protocol\":1,\"game\":\"jadeempire\",\"pid\":"); u2s(a, GetCurrentProcessId()); s_cat(out, a);
        s_cat(out, ",\"frames\":"); u2s(a, g_frames); s_cat(out, a);
        s_cat(out, ",\"mainLoopAgeMs\":"); if (age == 0xFFFFFFFF) s_cat(out, "-1"); else { u2s(a, age); s_cat(out, a); }
        s_cat(out, ",\"ready\":"); s_cat(out, (*G_APP && g_hooked) ? "true" : "false"); s_cat(out, "}}");
        reply(s, idtok, out); return;
    }
    if (s_eq(method, "version")) {
        char out[300]; s_cpy(out, "{\"result\":{\"protocol\":1,\"game\":\"jadeempire\",\"build\":\"steam-1.00-1510\",\"server\":\"jebridge-c 0.11.0\",\"pid\":");
        u2s(a, GetCurrentProcessId()); s_cat(out, a); s_cat(out, "}}"); reply(s, idtok, out); return;
    }
    if (s_eq(method, "methods")) {
        reply(s, idtok, "{\"result\":{\"methods\":[\"ping\",\"version\",\"methods\",\"je.state\",\"je.setDebugPanel\",\"je.runScript\",\"je.evalScript\",\"je.loadScript\",\"je.keepActive\",\"je.netStatus\",\"je.netPause\",\"je.netStart\",\"je.netTune\",\"je.peek\",\"je.playerPtr\",\"je.testSetDir\",\"je.watch\",\"je.evalMany\",\"je.creature\",\"je.pin\",\"je.call\",\"je.watchStats\",\"je.animLog\",\"je.animPlay\"]}}"); return;
    }
#ifdef JE_RE
    if (s_eq(method, "je.watchStats")) {   /* re/target: writer eips seen by the watch and how often */
        char out[900]; s_cpy(out, "{\"result\":{\"writers\":[");
        for (int k = 0; k < g_wseen; k++) { if (k) s_cat(out, ","); s_cat(out, "[\""); h2s(a, g_wsEip[k]); s_cat(out, a); s_cat(out, "\","); u2s(a, g_wsCnt[k]); s_cat(out, a); s_cat(out, "]"); }
        s_cat(out, "]}}"); reply(s, idtok, out); return;
    }
#endif
    if (s_eq(method, "je.watch")) {   /* bridge thread: the main thread is suspended while its DR0/DR1 are set */
        /* DEBUG: hardware write watch on the combat flag/table from the bridge thread */
        HANDLE t = OpenThread(0x1A /* GET|SET_CONTEXT|SUSPEND_RESUME */, 0, g_mainTid);
        if (!t) { reply(s, idtok, "{\"error\":{\"code\":-32004,\"message\":\"OpenThread failed\"}}"); return; }
        static uint32_t ctx[256]; SuspendThread(t); ctx[0] = 0x10010; /* CONTEXT_DEBUG_REGISTERS */
        int ok = GetThreadContext(t, ctx);
        uint32_t wa = 0; jnum_field(line, "oid", &wa); g_watchAddr = wa; g_wseen = 0;
        if (ok && wa) { ctx[1] = wa; ctx[5] = 0; ctx[6] = 0x000D0001; ok = SetThreadContext(t, ctx); }   /* re/target: one address, DR0, write, 4 bytes */
        else if (ok) { ctx[1] = cb_watch_addr(0); ctx[2] = cb_watch_addr(1); ctx[5] = 0; ctx[6] = 0x00DD0005;   /* L0 L1; RW0=01 LEN0=11 RW1=01 LEN1=11: write, 4 bytes */ ok = SetThreadContext(t, ctx); }
        ResumeThread(t); CloseHandle(t);
        char out[160]; s_cpy(out, "{\"result\":{\"ok\":"); s_cat(out, ok ? "true" : "false"); s_cat(out, ",\"dr0\":\""); h2s(a, cb_watch_addr(0)); s_cat(out, a);
        s_cat(out, "\",\"dr1\":\""); h2s(a, cb_watch_addr(1)); s_cat(out, a); s_cat(out, "\"}}"); reply(s, idtok, out); return;
    }
    if (!g_hooked) { reply(s, idtok, "{\"error\":{\"code\":-32010,\"message\":\"frame hook not installed\"}}"); return; }
    Req r; memset(&r, 0, sizeof r);
    s_cpy(r.method, method); jstr_field(line, "resref", r.resref, sizeof r.resref);
    if (!r.resref[0]) jstr_field(line, "name", r.resref, sizeof r.resref);
    if (!r.resref[0]) jstr_field(line, "names", r.resref, sizeof r.resref);
    { const char *h = s_str(line, "\"hex\""); if (h) { h += 5; while (*h == ' ' || *h == ':') h++; if (*h == '"') { h++; r.hex = h; while (h[r.hexLen] && h[r.hexLen] != '"') r.hexLen++; } } }
    r.oid = 0x7F000000; jnum_field(line, "oid", &r.oid);
    jnum_field(line, "fn", &r.cf[0]); jnum_field(line, "ecx", &r.cf[1]); jnum_field(line, "n", &r.cf[2]); jnum_field(line, "a", &r.cf[3]); jnum_field(line, "b", &r.cf[4]);
    { uint32_t v; if (jnum_field(line, "value", &v)) { r.ival = (int)v; r.hasIval = 1; } }
    uint32_t tmo = 30000; jnum_field(line, "timeoutMs", &tmo);
    r.deadline = GetTickCount() + tmo; r.ev = CreateEventA(0, 1, 0, 0);
    EnterCriticalSection(&g_cs);
    int full = g_qn >= QMAX; if (!full) g_q[g_qn++] = &r;
    LeaveCriticalSection(&g_cs);
    if (full) { CloseHandle(r.ev); reply(s, idtok, "{\"error\":{\"code\":-32003,\"message\":\"queue full\"}}"); return; }
    if (WaitForSingleObject(r.ev, tmo + 2000) != 0) {
        /* remove from queue so it never runs later */
        EnterCriticalSection(&g_cs);
        for (int i = 0; i < g_qn; i++) if (g_q[i] == &r) { for (int j = i + 1; j < g_qn; j++) g_q[j-1] = g_q[j]; g_qn--; break; }
        r.abandoned = 1;
        LeaveCriticalSection(&g_cs);
        if (!r.done) { reply(s, idtok, "{\"error\":{\"code\":-32001,\"message\":\"main thread did not run the request in time\"}}"); return; }
        WaitForSingleObject(r.ev, 0xFFFFFFFF);
    }
    CloseHandle(r.ev);
    if (r.done == 2) { reply(s, idtok, "{\"error\":{\"code\":-32001,\"message\":\"request expired before a frame ran it\"}}"); return; }
    reply(s, idtok, r.out);
}

static DWORD WINAPI client_thread(void *p) {
    SOCKET c = (SOCKET)p; char buf[8192]; int n = 0;
    for (;;) {
        int k = recv(c, buf + n, (int)sizeof buf - 1 - n, 0);
        if (k <= 0) break; n += k; buf[n] = 0;
        char *start = buf, *nl;
        while ((nl = (char *)s_str(start, "\n"))) {
            *nl = 0; if (nl > start && nl[-1] == '\r') nl[-1] = 0;
            if (*start) handle_line(c, start);
            start = nl + 1;
        }
        int rest = (int)s_len(start); memcpy(buf, start, rest); n = rest;
        if (n >= (int)sizeof buf - 1) { reply(c, "null", "{\"error\":{\"code\":-32700,\"message\":\"line too long\"}}"); break; }
    }
    closesocket(c); return 0;
}

/* File transport: requests are files bridge\\in\\*.json (one JSON line each); the reply is written
   to bridge\\out\\<same name> via a .tmp file and a rename, then the request file is deleted.
   Lets a client that cannot reach this PC's loopback (e.g. a VM sharing the folder) use the bridge. */
static DWORD WINAPI file_thread(void *unused) {
    (void)unused;
    char inDir[300], outDir[300], pat[320];
    s_cpy(inDir, g_dir); s_cat(inDir, "bridge"); CreateDirectoryA(inDir, 0);
    s_cpy(outDir, inDir); s_cat(outDir, "\\out\\"); s_cat(inDir, "\\in\\");
    CreateDirectoryA(inDir, 0); CreateDirectoryA(outDir, 0);
    s_cpy(pat, inDir); s_cat(pat, "*.json");
    static char line[8192], out[8192];
    /* Requests left over from an earlier process must never run in this one: drop them. */
    { FINDDATA fd; HANDLE fh = FindFirstFileA(pat, &fd); int dropped = 0;
      if (fh != (HANDLE)-1) { do { char rp[600]; s_cpy(rp, inDir); s_cat(rp, fd.name); DeleteFileA(rp); dropped++; } while (FindNextFileA(fh, &fd)); FindClose(fh); }
      if (dropped) logline("jebridge: dropped stale request files from an earlier run"); }
    for (;;) {
        FINDDATA fd; HANDLE fh = FindFirstFileA(pat, &fd);
        if (fh != (HANDLE)-1) {
            do {
                char rp[600], tp[600], op[600];
                s_cpy(rp, inDir); s_cat(rp, fd.name);
                HANDLE h = CreateFileA(rp, 0x80000000, 1, 0, 3, 0x80, 0);
                if (h == (HANDLE)-1) continue;
                DWORD n = 0; ReadFile(h, line, sizeof line - 1, &n, 0); CloseHandle(h); line[n] = 0;
                for (DWORD i = 0; i < n; i++) if (line[i] == '\r' || line[i] == '\n') line[i] = ' ';
                out[0] = 0; g_fileOut = out;
                handle_line(0, line);
                g_fileOut = 0;
                s_cpy(tp, outDir); s_cat(tp, fd.name); s_cat(tp, ".tmp");
                s_cpy(op, outDir); s_cat(op, fd.name);
                h = CreateFileA(tp, 0x40000000, 0, 0, 2, 0x80, 0);
                if (h != (HANDLE)-1) { DWORD w; WriteFile(h, out, (DWORD)s_len(out), &w, 0); CloseHandle(h); DeleteFileA(op); MoveFileA(tp, op); }
                DeleteFileA(rp);
            } while (FindNextFileA(fh, &fd));
            FindClose(fh);
        }
        Sleep(50);
    }
}

static DWORD WINAPI boot_thread(void *unused) {
    (void)unused;
    char msg[200];
    /* wait for the unpacked image and app object */
    for (int i = 0; i < 600 && !check_game(); i++) Sleep(100);
    if (!check_game()) { logline("jebridge: image check failed; not hooking (wrong game build?)"); return 0; }
    for (int i = 0; i < 1200 && !hook_iat(); i++) Sleep(50);
    g_hooked = g_realPeek != 0;
    logline(patch_callsites() ? "jebridge: LoadScript call sites patched (in-memory scripts on)" : "jebridge: LoadScript call sites did not match; in-memory scripts off");
    logline(g_hooked ? "jebridge: PeekMessageA IAT hook installed" : "jebridge: PeekMessageA not found in IAT");

    { char kp[300]; s_cpy(kp, g_dir); s_cat(kp, "bridge\\keep_active");
      HANDLE kh = CreateFileA(kp, 0x80000000, 1, 0, 3, 0x80, 0);
      if (kh != (HANDLE)-1) { CloseHandle(kh); g_keepActive = 1; logline("jebridge: keep_active marker found: world keeps running when unfocused"); } }
    CreateThread(0, 0, file_thread, 0, 0, 0);
    WSADATA_ wd; WSAStartup(0x202, &wd);
    g_netOn = net_parse_config();
    SOCKET ls = socket(2, 1, 6);
    SOCKADDR_IN4 sa; memset(&sa, 0, sizeof sa); sa.sin_family = 2; sa.addr = 0x0100007F; /* 127.0.0.1 */
    uint16_t port = 0;
    for (uint16_t p = 47310; p < 47330; p++) { sa.sin_port = (unsigned short)((p >> 8) | (p << 8)); if (bind(ls, &sa, sizeof sa) == 0) { port = p; break; } }
    if (!port || listen(ls, 4) != 0) { logline("jebridge: bind/listen failed"); return 0; }
    g_port = port;
    /* endpoint file */
    char path[300], js[200], a[16]; s_cpy(path, g_dir); s_cat(path, "coopbridge.json");
    s_cpy(js, "{\"protocol\": 1, \"game\": \"jadeempire\", \"pid\": "); u2s(a, GetCurrentProcessId()); s_cat(js, a);
    s_cat(js, ", \"endpoints\": [\"tcp://127.0.0.1:"); u2s(a, port); s_cat(js, a); s_cat(js, "\"]}\n");
    HANDLE h = CreateFileA(path, 0x40000000, 1, 0, 2 /*CREATE_ALWAYS*/, 0x80, 0);
    if (h != (HANDLE)-1) { DWORD w; WriteFile(h, js, (DWORD)s_len(js), &w, 0); CloseHandle(h); }
    s_cpy(msg, "jebridge: listening on tcp://127.0.0.1:"); u2s(a, port); s_cat(msg, a); logline(msg);
    for (;;) {
        SOCKET c = accept(ls, 0, 0);
        if (c == INVALID_SOCKET) { Sleep(100); continue; }
        CreateThread(0, 0, client_thread, (void *)c, 0, 0);
    }
}

/* ---------------- XInput pass-through to the game's own DLL (renamed xinput_orig.dll) ---------------- */
static HMODULE g_xi;
static void *xi(const char *name) {
    if (!g_xi) { char p[300]; s_cpy(p, g_dir); s_cat(p, "xinput_orig.dll"); g_xi = LoadLibraryA(p); }
    return g_xi ? GetProcAddress(g_xi, name) : 0;
}
__declspec(dllexport) DWORD WINAPI XInputGetState(DWORD i, void *st) {
    DWORD (WINAPI *f)(DWORD, void*) = xi("XInputGetState"); return f ? f(i, st) : 1167; /* ERROR_DEVICE_NOT_CONNECTED */
}
__declspec(dllexport) DWORD WINAPI XInputSetState(DWORD i, void *v) {
    DWORD (WINAPI *f)(DWORD, void*) = xi("XInputSetState"); return f ? f(i, v) : 1167;
}
__declspec(dllexport) DWORD WINAPI XInputGetCapabilities(DWORD i, DWORD fl, void *c) {
    DWORD (WINAPI *f)(DWORD, DWORD, void*) = xi("XInputGetCapabilities"); return f ? f(i, fl, c) : 1167;
}

/* ---------------- optional: allow several copies of the game at once (test installs) ---------------- */
/* WinMain-side check 0x004A3A40 does OpenMutexA/CreateMutexA("JadeEmpire") (pushes at 0x004A3A6B and
   0x004A3A9A). With bridge\\multi_instance present we point both pushes at a per-process name.
   Runs in DllMain: the PECompact stub has already unpacked .text when it loads our DLL. */
static char g_mutexName[32];
static void maybe_multi_instance(void) {
    char kp[300]; s_cpy(kp, g_dir); s_cat(kp, "bridge\\multi_instance");
    HANDLE kh = CreateFileA(kp, 0x80000000, 1, 0, 3, 0x80, 0);
    if (kh == (HANDLE)-1) return;
    CloseHandle(kh);
    static const uint32_t sites[2] = { 0x004A3A6B, 0x004A3A9A };
    for (int i = 0; i < 2; i++) {
        uint8_t *p = (uint8_t *)sites[i];
        if (IsBadReadPtr(p, 5) || p[0] != 0x68 || *(uint32_t *)(p + 1) != 0x006EC73C) { logline("jebridge: multi_instance: mutex push sites did not match; not patched"); return; }
    }
    char a[16]; s_cpy(g_mutexName, "JadeEmpire_jeb_"); u2s(a, GetCurrentProcessId()); s_cat(g_mutexName, a);
    for (int i = 0; i < 2; i++) {
        uint8_t *p = (uint8_t *)sites[i]; DWORD old;
        VirtualProtect(p, 5, 0x40, &old); *(uint32_t *)(p + 1) = (uint32_t)(uintptr_t)g_mutexName; VirtualProtect(p, 5, old, &old);
    }
    logline("jebridge: multi_instance: single-instance mutex made per-process");
}

BOOL WINAPI DllMain(HMODULE mod, DWORD reason, void *res) {
    (void)res;
    if (reason == 1) {
        GetModuleFileNameA(mod, g_dir, sizeof g_dir);
        size_t n = s_len(g_dir); while (n && g_dir[n-1] != '\\') n--; g_dir[n] = 0;
        InitializeCriticalSection(&g_cs);
        logline("jebridge: loaded");
        g_self = mod; AddVectoredExceptionHandler(1, veh);
        maybe_multi_instance();
        CreateThread(0, 0, boot_thread, 0, 0, 0);
    }
    return 1;
}
