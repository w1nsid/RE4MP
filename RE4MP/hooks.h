#pragma once
#include "SigScan.h"

// --- ESL spawning types (from RE4 SDK) ---

#pragma pack(push, 1)
struct SVEC { int16_t x, y, z; };

struct EM_LIST {
    uint8_t  be_flag_0;
    char     id_1;
    char     type_2;
    char     set_3;
    uint32_t flag_4;
    int16_t  hp_8;
    uint8_t  emset_no_A;
    char     Character_B;
    SVEC     s_pos_C;
    SVEC     s_ang_12;
    uint16_t room_18;
    int16_t  Guard_r_1A;
    uint16_t percentageMotionSpeed_1C;
    uint16_t percentageScale_1E;
};
#pragma pack(pop)

// --- Function typedefs ---

typedef BOOL(__cdecl* fn_RouteCkToPos)(void* cEm, float* pPos, float* pDest, uint32_t mode, float* pMax);
fn_RouteCkToPos RouteCkToPos;

typedef int* (__cdecl* fn_EmSetEvent)(EM_LIST* emList);
fn_EmSetEvent EmSetEvent = NULL;

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

float* GetCEmAng(int* cEmAddr)
{
    return (float*)((DWORD)cEmAddr + 0xA0);
}

bool IsCEmValid(int* cEmAddr)
{
    if (!cEmAddr) return false;
    uint32_t flags = *(uint32_t*)((DWORD)cEmAddr + 0x04);
    return (flags & 0x601) != 0;
}

uint16_t GetCurrentRoom()
{
    if (!g_pGlobalWK) return 0;
    return *(uint16_t*)((uint8_t*)g_pGlobalWK + 0x4FAC);
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

    // --- EmSetEvent: caller pattern → two-level CALL resolution ---
    SIG_FIND(EmSetEvent, "52 66 89 45 E4 66 89 4D F6 E8")
        if (_m) {
            uint8_t* thunk = SigResolveCall(_m, 9);
            if (thunk && (thunk[0] == 0xE8 || thunk[0] == 0xE9)) {
                uint8_t* fn = SigResolveCall(thunk, 0);
                EmSetEvent = (fn_EmSetEvent)fn;
                DbgLog("[SIG]     -> thunk 0x%08X, fn 0x%08X\n", (DWORD)thunk, (DWORD)fn);
            } else if (thunk) {
                // Thunk might have a short prologue before the CALL — scan first 32 bytes
                for (int i = 0; i < 32; i++) {
                    if (thunk[i] == 0xE8) {
                        uint8_t* fn = SigResolveCall(thunk, i);
                        EmSetEvent = (fn_EmSetEvent)fn;
                        DbgLog("[SIG]     -> thunk 0x%08X +%d, fn 0x%08X\n", (DWORD)thunk, i, (DWORD)fn);
                        break;
                    }
                }
            }
        }
    } while(0);

    DbgLog("[SIG] Resolved %d / %d signatures\n", found, total);

    g_sigsResolved = (g_pPL != NULL && g_pEmMgr != NULL && g_pGlobalWK != NULL);
    return g_sigsResolved;
}

// Spawn a Leon clone entity via the ESL system
int* SpawnLeonClone(float* spawnPos)
{
    if (!EmSetEvent) {
        DbgLog("[RE4MP] EmSetEvent not resolved — cannot spawn Leon clone\n");
        return NULL;
    }

    EM_LIST entry;
    memset(&entry, 0, sizeof(entry));
    entry.be_flag_0 = 0x03;    // ALIVE | SET
    entry.id_1      = 0x02;    // Leon
    entry.type_2    = 0;
    entry.hp_8      = 10000;   // high HP
    entry.room_18   = GetCurrentRoom();

    if (spawnPos) {
        entry.s_pos_C.x = (int16_t)spawnPos[0];
        entry.s_pos_C.y = (int16_t)spawnPos[1];
        entry.s_pos_C.z = (int16_t)spawnPos[2];
    }

    DbgLog("[RE4MP] EmSetEvent(id=0x02 room=%d pos=%.0f,%.0f,%.0f)\n",
           entry.room_18,
           spawnPos ? spawnPos[0] : 0.f,
           spawnPos ? spawnPos[1] : 0.f,
           spawnPos ? spawnPos[2] : 0.f);

    int* entity = EmSetEvent(&entry);
    if (entity)
        DbgLog("[RE4MP] Leon clone spawned at 0x%p\n", entity);
    else
        DbgLog("[RE4MP] EmSetEvent returned NULL\n");

    return entity;
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
