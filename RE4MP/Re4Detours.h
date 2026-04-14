#pragma once
#include "hooks.h"
#include "Cache.h"

// Hook RouteCkToPos: when the game pathfinds for our partner entity,
// redirect the destination to the remote player's position.
BOOL __cdecl HookedRouteCkToPos(void* cEm, float* pPos, float* pDest, uint32_t mode, float* pMax)
{
    __try {
        if (playerTwoPtr != nullptr && playerTwoReady && (void*)playerTwoPtr == cEm)
        {
            return RouteCkToPos(cEm, pPos, playerTwoPos, mode, pMax);
        }
        return RouteCkToPos(cEm, pPos, pDest, mode, pMax);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("[RE4MP] CRASH caught in HookedRouteCkToPos\n");
        return FALSE;
    }
}


static bool g_detoursAttached = false;

void InitDetourAddresses(DWORD base_addr)
{
    // RouteCkToPos may already be resolved by sig scan
    if (!RouteCkToPos)
        RouteCkToPos = (fn_RouteCkToPos)(base_addr + 0x02B2950);
}

void AttachDetours()
{
    if (g_detoursAttached) return;
    if (!RouteCkToPos) return;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)RouteCkToPos, HookedRouteCkToPos);
    DetourTransactionCommit();
    g_detoursAttached = true;
}

void DetourFunctions(DWORD base_addr)
{
    InitDetourAddresses(base_addr);
}
