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
static uint16_t* g_pRoomId = NULL;   // -> current room/area ID (2 bytes)

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

float* GetCEmRot(int* cEmAddr)
{
    return (float*)((DWORD)cEmAddr + 0xA0);  // cCoord::ang_A0
}

int16_t GetCEmHP(int* cEmAddr)
{
    return *(int16_t*)((DWORD)cEmAddr + 0x324);  // cEm::hp_324
}

// Animation accessors — offsets from cModel/MOTION_INFO layout (re4_tweaks SDK)
uint8_t GetCEmRoutine(int* cEmAddr)
{
    return *(uint8_t*)((DWORD)cEmAddr + 0xFC);   // cModel::r_no_0_FC
}

int32_t GetCEmAnimSeq(int* cEmAddr)
{
    return *(int32_t*)((DWORD)cEmAddr + 0x284);  // Motion_1D8 + Seq_AC
}

float GetCEmAnimFrame(int* cEmAddr)
{
    return *(float*)((DWORD)cEmAddr + 0x1FC);    // Motion_1D8 + Mot_frame_24
}

float GetCEmAnimSpeed(int* cEmAddr)
{
    return *(float*)((DWORD)cEmAddr + 0x298);    // Motion_1D8 + Seq_speed_C0
}

void SetCEmAnimSeq(int* cEmAddr, int32_t seq)
{
    *(int32_t*)((DWORD)cEmAddr + 0x284) = seq;
}

void SetCEmAnimFrame(int* cEmAddr, float frame)
{
    *(float*)((DWORD)cEmAddr + 0x1FC) = frame;
}

void SetCEmAnimSpeed(int* cEmAddr, float speed)
{
    *(float*)((DWORD)cEmAddr + 0x298) = speed;
}

uint8_t GetCEmId(int* cEmAddr)
{
    return *(uint8_t*)((DWORD)cEmAddr + 0x100);  // cModel::id_100
}

uint8_t GetCEmListIndex(int* cEmAddr)
{
    return *(uint8_t*)((DWORD)cEmAddr + 0x3A0);  // cEm::emListIndex_3A0
}

bool IsCEmValid(int* cEmAddr)
{
    uint32_t flags = *(uint32_t*)((DWORD)cEmAddr + 0x4);  // cUnit::be_flag_4
    return (flags & 0x601) != 0;
}

// Entity type classification matching re4_tweaks IsEnemy/IsGanado
bool IsEnemyId(uint8_t id)
{
    // Blacklist non-enemy objects that fall in the enemy ID range
    if (id == 0x2A) return false;  // Mine/bear trap
    if (id == 0x3B) return false;  // Truck/Wagon
    if (id == 0x3D) return false;  // Mike's helicopter
    if (id == 0x4E) return false;  // SW Ship cannon
    return (id > 0x10 && id < 0x4F);
}

bool IsBossId(uint8_t id)
{
    // Boss entity IDs (chapter bosses, mini-bosses)
    switch (id) {
        case 0x22: // Del Lago
        case 0x23: // El Gigante
        case 0x24: // El Gigante (variant)
        case 0x30: // Garrador
        case 0x31: // Garrador (armored)
        case 0x32: // Verdugo
        case 0x33: // Pesanta / U-3
        case 0x34: // Salazar statue
        case 0x39: // Krauser (mutant)
        case 0x3A: // Saddler
        case 0x3E: // It (U-3 second form)
        case 0x42: // Krauser (human)
        case 0x46: // Mendez
        case 0x47: // Mendez (mutant)
            return true;
        default:
            return false;
    }
}

// --- Entity Manager iteration ---
// cEmMgr layout (from re4_tweaks SDK/cManager.h):
//   +0x04: T* m_Array_4        (raw entity block)
//   +0x08: uint32_t m_nArray_8 (total entity slots)
//   +0x0C: uint32_t m_blockSize_C (bytes per entity, 0x408 for cEm)
//   +0x14: T* m_pAlive_14      (linked list head)
//   +0x18: T* m_pAliveBack_18  (linked list tail)

// Get entity count from EmMgr
uint32_t GetEmCount()
{
    if (!g_pEmMgr) return 0;
    return *(uint32_t*)((DWORD)g_pEmMgr + 0x08);
}

// Get entity block size from EmMgr
uint32_t GetEmBlockSize()
{
    if (!g_pEmMgr) return 0x408; // default cEm size
    return *(uint32_t*)((DWORD)g_pEmMgr + 0x0C);
}

// Get entity by array index
int* GetEmByIndex(uint32_t idx)
{
    if (!g_pEmMgr) return NULL;
    int* array = *(int**)((DWORD)g_pEmMgr + 0x04);
    if (!array) return NULL;
    uint32_t blockSize = GetEmBlockSize();
    return (int*)((uint8_t*)array + idx * blockSize);
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

uint16_t GetLocalRoomId()
{
    if (!g_pRoomId) return 0;
    return *g_pRoomId;
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
    if (!g_pRoomId)   g_pRoomId   = (uint16_t*)(base_addr + 0x85BE90);

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
