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

float* GetCEmRot(int* cEmAddr)
{
    return (float*)((DWORD)cEmAddr + 0xA0);  // cCoord::ang_A0
}

int16_t GetCEmHP(int* cEmAddr)
{
    return *(int16_t*)((DWORD)cEmAddr + 0x324);  // cEm::hp_324
}

void SetCEmHP(int* cEmAddr, int16_t hp)
{
    *(int16_t*)((DWORD)cEmAddr + 0x324) = hp;
}

// Animation accessors — offsets from cModel/MOTION_INFO layout (re4_tweaks SDK)
uint8_t GetCEmRoutine(int* cEmAddr)
{
    return *(uint8_t*)((DWORD)cEmAddr + 0xFC);   // cModel::r_no_0_FC
}

void SetCEmRoutine(int* cEmAddr, uint8_t routine)
{
    *(uint8_t*)((DWORD)cEmAddr + 0xFC) = routine;
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
    if (!g_pGlobalWK) return 0;
    return *(uint16_t*)((uint8_t*)g_pGlobalWK + 0x4FAC);  // GLOBAL_WK::curRoomId_4FAC
}

// --- Pause state ---
// flags_STOP_0_170: SPF_ bits that freeze different game subsystems.
// The game's pause screen sets 0xBFFFFF7F (everything except SPF_ID_SYSTEM).

uint32_t GetPauseFlags()
{
    if (!g_pGlobalWK) return 0;
    return *(uint32_t*)((uint8_t*)g_pGlobalWK + 0x170);  // GLOBAL_WK::flags_STOP_0_170
}

void SetPauseFlags(uint32_t flags)
{
    if (!g_pGlobalWK) return;
    *(uint32_t*)((uint8_t*)g_pGlobalWK + 0x170) = flags;
}

// --- Game state checks ---

// Returns true when the game is in normal gameplay (Rno0 == MainLoop == 3).
bool IsGameInMainLoop()
{
    if (!g_pGlobalWK) return false;
    return *(uint8_t*)((uint8_t*)g_pGlobalWK + 0x20) == 3;  // Rno0_20 == MainLoop
}

// Returns true when the game is NOT in the middle of a room transition.
// Safe to access entity pointers only when this returns true.
bool IsGameStable()
{
    if (!g_pGlobalWK) return false;
    uint8_t rno0 = *(uint8_t*)((uint8_t*)g_pGlobalWK + 0x20);
    return (rno0 == 3);  // Only MainLoop is stable
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

void HookFunctions(DWORD base_addr)
{
    DbgLog("[SIG] Starting signature scan...\n");
    bool ok = SigScanResolve();

    if (!ok) {
        DbgLog("[RE4MP] ERROR: Signature scan failed — cannot resolve game pointers\n");
        DbgLog("[RE4MP] Only v1.1.0+ is supported\n");
    }
}

// Prevent enemy AI from targeting Ashley for pickup/carry.
// Sets cEm10::Sub_pos_450 far away and l_sub distances high so
// enemies never attempt the grab action — fixes desync between
// host and client caused by differing AI pickup decisions.
void DisableEnemyAshleyPickup()
{
    if (!g_pEmMgr) return;
    uint32_t count = GetEmCount();
    uint32_t blockSize = GetEmBlockSize();

    for (uint32_t i = 0; i < count && i < MAX_SYNC_ENTITIES; i++) {
        int* em = GetEmByIndex(i);
        if (!em || !IsCEmValid(em)) continue;

        uint8_t id = GetCEmId(em);
        if (!IsEnemyId(id) && !IsBossId(id)) continue;

        __try {
            // cEm::l_sub_37C — distance to subchar (all entity types)
            *(float*)((DWORD)em + 0x37C) = 99999.0f;

            // cEm10-specific fields (only safe when blockSize >= cEm10 size)
            if (blockSize >= 0x690) {
                // cEm10::Sub_pos_450 — cached Ashley position used by AI
                float* subPos = (float*)((DWORD)em + 0x450);
                subPos[0] = 99999.0f;
                subPos[1] = 99999.0f;
                subPos[2] = 99999.0f;

                // cEm10::L_sub_684 — computed distance to subchar
                *(float*)((DWORD)em + 0x684) = 99999.0f;

                // cEm10::L_sub_route_698 — route distance to subchar
                *(float*)((DWORD)em + 0x698) = 99999.0f;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
}
