// wuwapad - feed gamepad input into Wuwa Tappo (Wuthering Waves desktop pet)
//           without touching global input.
//
// How it works
//   1. Injected (LoadLibrary) into the WuwaTappo.exe process, x64.
//   2. Hooks user32!CallNextHookEx via IAT patch to learn the app's own
//      WH_KEYBOARD_LL hook procedures (RtlLookupFunctionEntry on the caller).
//   3. Polls XInput and, on a button/stick event, calls that hook procedure
//      directly with a fabricated KBDLLHOOKSTRUCT (WM_KEYDOWN + WM_KEYUP).
//      Nothing is sent to the system input queue: other programs see nothing.
//
// Build: build.bat (MSVC x64).  See README.md.
//

// wuwapad v3 - gamepad -> Wuwa Tappo's WH_KEYBOARD_LL proc.
// One clean press per button: WM_KEYDOWN then WM_KEYUP (configurable).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <intrin.h>

typedef LRESULT (WINAPI *PFN_CNH)(HHOOK, int, WPARAM, LPARAM);
typedef LRESULT (WINAPI *PFN_LLPROC)(int, WPARAM, LPARAM);
typedef DWORD   (WINAPI *PFN_XIGS)(DWORD, void*);
typedef PRUNTIME_FUNCTION (WINAPI *PFN_LOOKUP)(DWORD64, DWORD64*, void*);

static PFN_CNH     g_realCNH = NULL;
static PFN_LLPROC  g_llProc  = NULL;
static PFN_XIGS    g_xinput  = NULL;

typedef SHORT (WINAPI *PFN_GAKS)(int);
typedef SHORT (WINAPI *PFN_GKS)(int);
typedef BOOL  (WINAPI *PFN_GKBS)(PBYTE);
static PFN_GAKS g_realGAKS = NULL;
static PFN_GKS  g_realGKS  = NULL;
static PFN_GKBS g_realGKBS = NULL;
static volatile LONG g_fakeVk = 0;
static volatile LONG g_fakeUntil = 0;

static BOOL FakeDown(int vk)
{
    LONG u = g_fakeUntil;
    return (LONG)g_fakeVk == vk && u && (LONG)(GetTickCount() - u) < 0;
}

static SHORT WINAPI HookGAKS(int vk) { if (FakeDown(vk)) return (SHORT)0x8001; return g_realGAKS ? g_realGAKS(vk) : 0; }
static SHORT WINAPI HookGKS(int vk)  { if (FakeDown(vk)) return (SHORT)0x0001; return g_realGKS  ? g_realGKS(vk)  : 0; }
static BOOL WINAPI HookGKBS(PBYTE st)
{
    BOOL r = g_realGKBS ? g_realGKBS(st) : FALSE;
    if (st) { LONG fv = g_fakeVk; if (fv && FakeDown((int)fv)) st[fv] = 0x80; }
    return r;
}

static void LogF(const char *fmt, ...)
{
    char path[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, path)) return;
    lstrcatA(path, "wuwapad.log");
    FILE *f = fopen(path, "a");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static PFN_LLPROC g_procs[8];
static int        g_procCount = 0;

static void AddProc(void *retAddr)
{
    if (!retAddr || g_procCount >= 8) return;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return;
    PFN_LOOKUP lookup = (PFN_LOOKUP)GetProcAddress(ntdll, "RtlLookupFunctionEntry");
    if (!lookup) return;
    DWORD64 base = 0;
    PRUNTIME_FUNCTION rf = lookup((DWORD64)(ULONG_PTR)retAddr, &base, NULL);
    if (!rf) return;
    PFN_LLPROC p = (PFN_LLPROC)(ULONG_PTR)(base + rf->BeginAddress);
    for (int i = 0; i < g_procCount; i++) if (g_procs[i] == p) return;
    g_procs[g_procCount++] = p;
    if (!g_llProc) g_llProc = p;
    LogF("hook proc #%d: %p", g_procCount, (void*)p);
}

static volatile LONG g_realKeys = 0;
static LRESULT WINAPI HookCNH(HHOOK hk, int code, WPARAM w, LPARAM l)
{
    if (code == HC_ACTION && (w == WM_KEYDOWN || w == WM_KEYUP || w == WM_SYSKEYDOWN || w == WM_SYSKEYUP))
        AddProc(_ReturnAddress());
    if (code == HC_ACTION && (w == WM_KEYDOWN || w == WM_SYSKEYDOWN)) {
        LONG n = InterlockedIncrement(&g_realKeys);
        if (n % 20 == 1) {
            KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT*)l;
            LogF("real key #%ld vk=0x%X flags=0x%X", n, kb ? kb->vkCode : 0, kb ? kb->flags : 0);
        }
    }
    return g_realCNH ? g_realCNH(hk, code, w, l) : 0;
}

static void* PatchIAT(HMODULE mod, const char *dllName, const char *funcName, void *hook)
{
    BYTE *base = (BYTE*)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return NULL;
    IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress);
    for (; imp->Name; imp++) {
        const char *name = (const char*)(base + imp->Name);
        if (_stricmp(name, dllName) != 0) continue;
        IMAGE_THUNK_DATA *orig = (IMAGE_THUNK_DATA*)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        IMAGE_THUNK_DATA *iat  = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        for (; orig->u1.Function; orig++, iat++) {
            if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME*)(base + orig->u1.AddressOfData);
            if (strcmp((const char*)ibn->Name, funcName) != 0) continue;
            void **slot = (void**)&iat->u1.Function;
            void *old = *slot;
            DWORD prot;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &prot)) return NULL;
            *slot = hook;
            VirtualProtect(slot, sizeof(void*), prot, &prot);
            FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
            return old;
        }
    }
    return NULL;
}

// ---- config: %TEMP%\wuwapad_cfg.txt  (optional) ----
static int  g_sendUp    = 1;
static int  g_upDelayMs = 12;
static int  g_stick     = 1;
static int  g_procOnly  = 0;   // 0 = all, N = only proc #N
static int  g_debounce  = 120; // ms
static int  g_sdz       = 16000;
static int  g_srz       = 8000;
static DWORD g_cfgStamp = 0;

static void LoadCfg(void)
{
    char path[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, path)) return;
    lstrcatA(path, "wuwapad_cfg.txt");
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) return;
    DWORD stamp = fad.ftLastWriteTime.dwLowDateTime;
    if (stamp == g_cfgStamp) return;
    g_cfgStamp = stamp;

    char buf[512] = {0};
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD got = 0; ReadFile(f, buf, sizeof(buf)-1, &got, NULL); CloseHandle(f);

    char *p;
    if ((p = strstr(buf, "up=")) != NULL)        g_sendUp = atoi(p + 3) ? 1 : 0;
    if ((p = strstr(buf, "updelay=")) != NULL)   { int v = atoi(p + 8); if (v >= 0 && v <= 500) g_upDelayMs = v; }
    if ((p = strstr(buf, "stick=")) != NULL)     g_stick = atoi(p + 6) ? 1 : 0;
    if ((p = strstr(buf, "proc=")) != NULL)      g_procOnly = atoi(p + 5);
    if ((p = strstr(buf, "debounce=")) != NULL)  { int v = atoi(p + 9); if (v >= 0 && v <= 1000) g_debounce = v; }
    if ((p = strstr(buf, "sdz=")) != NULL)       { int v = atoi(p + 4); if (v > 1000 && v < 32000) g_sdz = v; }
    if ((p = strstr(buf, "srz=")) != NULL)       { int v = atoi(p + 4); if (v >= 0 && v < 32000) g_srz = v; }
    LogF("cfg: sendUp=%d upDelay=%d", g_sendUp, g_upDelayMs);
}

static void TapKey(WORD vk)
{
    if (!g_llProc) { LogF("TapKey: proc unknown"); return; }
    LoadCfg();

    KBDLLHOOKSTRUCT kb;
    ZeroMemory(&kb, sizeof(kb));
    kb.vkCode = vk;
    kb.scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    kb.time = GetTickCount();

    InterlockedExchange(&g_fakeVk, (LONG)vk);
    InterlockedExchange(&g_fakeUntil, (LONG)(GetTickCount() + 200));
    for (int i = 0; i < g_procCount; i++)
        if (g_procs[i] && (g_procOnly == 0 || g_procOnly == i + 1)) g_procs[i](HC_ACTION, WM_KEYDOWN, (LPARAM)&kb);
    InterlockedExchange(&g_fakeUntil, 0);
    if (g_sendUp) {
        Sleep(g_upDelayMs);
        kb.time = GetTickCount();
        kb.flags = 0;
        for (int i = 0; i < g_procCount; i++)
            if (g_procs[i] && (g_procOnly == 0 || g_procOnly == i + 1)) g_procs[i](HC_ACTION, WM_KEYUP, (LPARAM)&kb);
    }
}

struct MAP { WORD button; WORD vk; const char *name; };
static const struct MAP g_map[] = {
    { 0x1000,'J',"A" },{ 0x2000,'K',"B" },{ 0x4000,'U',"X" },{ 0x8000,'I',"Y" },
    { 0x0100,'H',"LB" },{ 0x0200,'N',"RB" },{ 0x0020,'M',"Back" },{ 0x0010,'O',"Start" },
    { 0x0040,'R',"LS" },{ 0x0080,'F',"RS" },
    { 0x0001,'W',"Up" },{ 0x0002,'S',"Down" },{ 0x0004,'A',"Left" },{ 0x0008,'D',"Right" },
    { 0,0,0 }
};
typedef struct {
    DWORD packet;
    WORD  buttons;
    BYTE  lt, rt;
    SHORT lx, ly, rx, ry;
} PADSTATE;

typedef struct { int up, down, left, right; } STICKARM;
static STICKARM g_lsArm[4], g_rsArm[4];


static void StickDir(int slot, int isRight, int dir, WORD vk)
{
    STICKARM *a = isRight ? &g_rsArm[slot] : &g_lsArm[slot];
    int *state = dir == 0 ? &a->up : dir == 1 ? &a->down : dir == 2 ? &a->left : &a->right;
    if (*state == 0) { *state = 1; TapKey(vk); }
}

static void StickAxes(int slot, PADSTATE *c)
{
    if (!g_stick) return;
    struct AX { int axis; int isRight; int dir; WORD vk; } ax[8] = {
        { c->ly, 0, 0, 'T' }, { c->ly, 0, 1, 'G' }, { c->lx, 0, 2, 'C' }, { c->lx, 0, 3, 'V' },
        { c->ry, 1, 0, 'Y' }, { c->ry, 1, 1, 'B' }, { c->rx, 1, 2, 'L' }, { c->rx, 1, 3, 'P' },
    };
    for (int i = 0; i < 8; i++) {
        int v = ax[i].axis;
        int active = (ax[i].dir == 0 || ax[i].dir == 3) ? (v >  g_sdz) : (v < -g_sdz);
        int idle   = (ax[i].dir == 0 || ax[i].dir == 3) ? (v <  g_srz) : (v > -g_srz);
        STICKARM *a = ax[i].isRight ? &g_rsArm[slot] : &g_lsArm[slot];
        int *st = ax[i].dir == 0 ? &a->up : ax[i].dir == 1 ? &a->down : ax[i].dir == 2 ? &a->left : &a->right;
        if (active && *st == 0) { *st = 1; LogF("stick%c dir=%d -> vk 0x%X", ax[i].isRight ? 'R' : 'L', ax[i].dir, ax[i].vk); TapKey(ax[i].vk); }
        else if (idle && *st == 1) { *st = 0; }
    }
}

static void CheckTrigger(void)
{
    char path[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, path)) return;
    lstrcatA(path, "wuwapad_trigger.txt");
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return;
    char buf[512] = {0};
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD got = 0; ReadFile(f, buf, sizeof(buf)-1, &got, NULL); CloseHandle(f);
    DeleteFileA(path);
    char *p;
    if ((p = strstr(buf, "load="))) {
        char *nl = strpbrk(p + 5, "\r\n"); if (nl) *nl = 0;
        HMODULE m = LoadLibraryA(p + 5);
        LogF("load '%s' -> %p err=%lu", p + 5, (void*)m, GetLastError());
        return;
    }
    unsigned int vk = 0, count = 1, delay = 120;
    int parsed = sscanf(buf, "vk=0x%x", &vk);
    if (parsed != 1) parsed = sscanf(buf, "vk=%u", &vk);
    if ((p = strstr(buf, "count="))) count = (unsigned)atoi(p+6);
    if ((p = strstr(buf, "delay="))) delay = (unsigned)atoi(p+6);
    if (count == 0 || count > 100) count = 1;
    if (parsed == 1 && vk > 0 && vk < 0x100)
        for (unsigned i = 0; i < count; i++) { TapKey((WORD)vk); Sleep(delay); }
}

static int Calibrate(void)
{
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (!u) return 0;
    typedef UINT (WINAPI *PFN_SI)(UINT, LPINPUT, int);
    PFN_SI si = (PFN_SI)GetProcAddress(u, "SendInput");
    if (!si) return 0;
    INPUT in; ZeroMemory(&in, sizeof(in));
    in.type = INPUT_KEYBOARD; in.ki.wVk = VK_SHIFT;
    si(1, &in, sizeof(INPUT));
    Sleep(25);
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    si(1, &in, sizeof(INPUT));
    return 1;
}

static DWORD WINAPI Worker(LPVOID p)
{
    (void)p;
    HMODULE xi = LoadLibraryA("xinput1_4.dll");
    if (!xi) xi = LoadLibraryA("xinput1_3.dll");
    if (xi) g_xinput = (PFN_XIGS)GetProcAddress(xi, "XInputGetState");
    LoadCfg();
    LogF("worker start v9, xinput=%p sendUp=%d stick=%d sdz=%d", (void*)g_xinput, g_sendUp, g_stick, g_sdz);

    DWORD t0 = GetTickCount();
    int calib = 0;
    PADSTATE prev[4]; ZeroMemory(prev, sizeof(prev));
    DWORD lastTap[16]; ZeroMemory(lastTap, sizeof(lastTap));

    for (;;) {
        Sleep(6);
        if (!g_llProc && calib < 3 && GetTickCount() - t0 > 2000) {
            if (Calibrate()) LogF("calibration key sent (%d)", ++calib);
            t0 = GetTickCount();
        }
        if (!g_xinput) continue;
        for (DWORD s = 0; s < 4; s++) {
            PADSTATE cur; ZeroMemory(&cur, sizeof(cur));
            if (g_xinput(s, &cur) != 0) continue;
            if (cur.packet == prev[s].packet) continue;
            WORD pr = (WORD)(cur.buttons & ~prev[s].buttons);
            for (int i = 0; g_map[i].button; i++) {
                if (!(pr & g_map[i].button)) continue;
                DWORD now = GetTickCount();
                if (now - lastTap[i] < (DWORD)g_debounce) continue;
                lastTap[i] = now;
                LogF("pad%u press %s -> vk 0x%X", s, g_map[i].name, g_map[i].vk);
                TapKey(g_map[i].vk);
            }
            if (cur.lt > 40 && prev[s].lt <= 40) TapKey('Q');
            if (cur.rt > 40 && prev[s].rt <= 40) TapKey('E');
            StickAxes((int)s, &cur);
            prev[s] = cur;
        }
        CheckTrigger();
    }
    return 0;
}

static DWORD WINAPI Init(LPVOID p)
{
    (void)p;
    HMODULE mod = GetModuleHandleW(NULL);
    g_realCNH  = (PFN_CNH) PatchIAT(mod, "user32.dll", "CallNextHookEx",    (void*)HookCNH);
    g_realGAKS = (PFN_GAKS)PatchIAT(mod, "user32.dll", "GetAsyncKeyState",  (void*)HookGAKS);
    g_realGKS  = (PFN_GKS) PatchIAT(mod, "user32.dll", "GetKeyState",       (void*)HookGKS);
    g_realGKBS = (PFN_GKBS)PatchIAT(mod, "user32.dll", "GetKeyboardState",  (void*)HookGKBS);
    LogF("patched GAKS=%p GKS=%p GKBS=%p", (void*)g_realGAKS, (void*)g_realGKS, (void*)g_realGKBS);
    LogF("wuwapad v3 loaded, realCNH=%p", (void*)g_realCNH);
    Worker(NULL);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID res)
{
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        HANDLE once = CreateMutexA(NULL, FALSE, "Local\\wuwapad_singleton");
        if (once && GetLastError() == ERROR_ALREADY_EXISTS) return TRUE;   // already running in this process
        CreateThread(NULL, 0, Init, NULL, 0, NULL);
    }
    return TRUE;
}
