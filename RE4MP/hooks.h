#pragma once
#include "SigScan.h"

// --- Function typedefs ---

typedef BOOL(__cdecl* fn_RouteCkToPos)(void* cEm, float* pPos, float* pDest, uint32_t mode, float* pMax);
fn_RouteCkToPos RouteCkToPos;

// --- Resolved global pointers (set by SigScanResolve) ---
static int**   g_pPL  = NULL;   // -> cPlayer* (Leon)
static int**   g_pAS  = NULL;   // -> cPlayer* (Ashley/subchar)
static void*   g_pEmMgr = NULL; // -> cEmMgr
static uint32_t* g_pGlobalWK = NULL; // -> GLOBAL_WK

// Forward declaration for DbgLog (defined in RE4MP.cpp)
extern void DbgLog(const char* fmt, ...);

// --- Pointer accessors ---

int* PlayerPointer()
{
    if (!g_pPL || !*g_pPL) return NULL;
    return *g_pPL;
}

float* GetPlayerPosition()
{
    int* ptr = PlayerPointer();
    if (!ptr) return NULL;
    return (float*)((DWORD)ptr + 0x94);  // cCoord::pos_94
}

float* GetCEmPos(int* cEmAddr)
{
    return (float*)((DWORD)cEmAddr + 0x94);
}

int* SubCharPointer()
{
    if (!g_pAS || !*g_pAS) return NULL;
    return *g_pAS;
}

// Force STA_SUB_ASHLEY flag (index 101) to ensure Ashley is loaded in every area.
// Flag layout: flags_STATUS_0_501C is a 128-bit bitfield (4 x uint32_t).
// Index 101 → array[101/32]=array[3] → bit (0x80000000 >> (101&31)) = 0x04000000
// array[3] is at GLOBAL_WK offset 0x5028.
void ForceAshleyPresent()
{
    if (!g_pGlobalWK) return;
    uint32_t* flags = (uint32_t*)((uint8_t*)g_pGlobalWK + 0x501C);
    flags[3] |= 0x04000000; // STA_SUB_ASHLEY

    // Force Ashley armor costume (subCostume_4FCB: 0=Normal, 1=Popstar, 2=Armor)
    *(uint8_t*)((uint8_t*)g_pGlobalWK + 0x4FCB) = 2;
}

void PatchBytes(DWORD addr, unsigned char* bytes, int len)
{
    DWORD oldProtect;
    VirtualProtect((LPVOID)addr, len, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy((void*)addr, bytes, len);
    VirtualProtect((LPVOID)addr, len, oldProtect, &oldProtect);
}

// =====================================================================
// Signature-based resolution — works on v1.0.6 AND v1.1.0
// =====================================================================
// Patterns derived from re4_tweaks SDK and cross-referenced with v1.0.6 offsets.

static bool g_sigsResolved = false;

bool SigScanResolve()
{
    if (g_sigsResolved) return true;
    int found = 0, total = 0;

    #define SIG_FIND(name, pat) do { \
        uint8_t* _m = SigScan(pat, #name); \
        if (_m) { found++; DbgLog("[SIG]   " #name " found at 0x%08X\n", (DWORD)_m); } \
        else { DbgLog("[SIG]   " #name " NOT FOUND!\n"); } \
        total++; \

    // --- Global pointers ---

    // pPL (player pointer): MOV EAX, [addr] in float math context
    SIG_FIND(pPL, "A1 ? ? ? ? D8 CC D8 C9 D8 CA D9 5D ? D9 45 ?")
        if (_m) g_pPL = *(int***)(_m + 1);
    } while(0);

    // pAS (Ashley/subchar pointer): TEST AL, 02; JZ +16; MOV EDX, [addr]
    SIG_FIND(pAS, "A8 02 74 16 8B 15")
        if (_m) g_pAS = *(int***)(_m + 6);
    } while(0);

    // EmMgr: MOV ECX, addr (thiscall setup)
    SIG_FIND(EmMgr, "81 E1 01 02 00 00 83 F9 01 75 ? 50 B9")
        if (_m) g_pEmMgr = *(void**)(_m + 0xD);
    } while(0);

    // GLOBAL_WK pointer: MOV EAX, [addr]; MOV ECX, 7FFFFFFF; AND [EAX+?], ECX
    SIG_FIND(GlobalWK, "A1 ? ? ? ? B9 FF FF FF 7F 21 48 ? A1")
        if (_m) g_pGlobalWK = **(uint32_t***)(_m + 1);
    } while(0);

    // --- RouteCkToPos: find CALL then resolve destination ---
    SIG_FIND(RouteCkToPos, "E8 ? ? ? ? 83 C4 14 85 C0 74 ? 83 8E 08 04")
        if (_m) {
            uint8_t* dest = SigResolveCall(_m, 0);
            RouteCkToPos = (fn_RouteCkToPos)dest;
            DbgLog("[SIG]     -> resolved to 0x%08X\n", (DWORD)dest);
        }
    } while(0);

    DbgLog("[SIG] Resolved %d / %d signatures\n", found, total);

    g_sigsResolved = (g_pPL != NULL && g_pEmMgr != NULL && g_pGlobalWK != NULL);
    return g_sigsResolved;
}

// Fallback: use hardcoded v1.0.6 offsets if sigs fail
void HookFunctions_v106(DWORD base_addr)
{
    DbgLog("[RE4MP] Using hardcoded v1.0.6 offsets as fallback\n");

    if (!g_pPL)       g_pPL       = (int**)(base_addr + 0x857054);
    if (!g_pAS)       g_pAS       = (int**)(base_addr + 0x857060);
    if (!g_pEmMgr)    g_pEmMgr    = (void*)(base_addr + 0x7fDB04);
    if (!g_pGlobalWK) g_pGlobalWK = *(uint32_t**)(base_addr + 0x855A40);

    if (!RouteCkToPos)
        RouteCkToPos = (fn_RouteCkToPos)(base_addr + 0x02B2950);
}

void HookFunctions(DWORD base_addr)
{
    DbgLog("[SIG] Starting signature scan...\n");
    bool ok = SigScanResolve();

    if (!ok) {
        HookFunctions_v106(base_addr);
    }
}

void CodeInjection(DWORD base_addr)
{
    unsigned char twoNop[2] = { 0x90, 0x90 };
    unsigned char threeNop[3] = { 0x90, 0x90, 0x90 };
    unsigned char fiveNop[5] = { 0x90, 0x90, 0x90, 0x90, 0x90 };
    unsigned char sixNop[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

    // These NOP patches disable AI movement code for the partner character.
    // On v1.1.0 these offsets may differ — we skip patching if sig scan succeeded
    // and the game is NOT v1.0.6. We detect v1.0.6 by checking if pPL has the
    // known static address.
    DWORD ba = base_addr;
    bool is_v106 = ((DWORD)g_pPL == (ba + 0x857054));

    if (is_v106) {
        DbgLog("[RE4MP] Applying v1.0.6 NOP patches\n");

        // Disable Luis partner set and move
        PatchBytes((ba + 0x4e8a41), sixNop, 6);

        // Disable cSubChar setting partner location for movement
        PatchBytes((ba + 0x35e9fa), twoNop, 2);
        PatchBytes((ba + 0x35ea02), threeNop, 3);
        PatchBytes((ba + 0x35ea0b), threeNop, 3);
        PatchBytes((ba + 0x35e9cb), twoNop, 2);
        PatchBytes((ba + 0x35e9cd), threeNop, 3);
        PatchBytes((ba + 0x35e9d0), threeNop, 3);
        PatchBytes((ba + 0x35eb4c), fiveNop, 5);
        PatchBytes((ba + 0x35e9df), fiveNop, 5);
        PatchBytes((ba + 0x35eb00), fiveNop, 5);

        // spawn subChar everywhere
        PatchBytes((ba + 0x2c520b), twoNop, 2);
    } else {
        DbgLog("[RE4MP] Non-v1.0.6 detected — skipping NOP patches (sig-scan handles hooks)\n");
    }
}
