#pragma once
#include <Windows.h>
#include <stdint.h>

// Lightweight IDA-style pattern scanner for the main game module.
// Pattern format: hex bytes separated by spaces, '?' = wildcard.
// Example: "A1 ? ? ? ? D8 CC D8 C9"

static uint8_t* SigScan(const char* pattern, const char* name)
{
    // Parse pattern into bytes + mask
    uint8_t patBytes[256];
    bool    patMask[256]; // true = must match, false = wildcard
    int     patLen = 0;

    const char* p = pattern;
    while (*p) {
        while (*p == ' ') p++;
        if (*p == 0) break;
        if (*p == '?') {
            patBytes[patLen] = 0;
            patMask[patLen] = false;
            patLen++;
            p++;
            if (*p == '?') p++; // handle "??"
        } else {
            char hex[3] = { p[0], p[1], 0 };
            patBytes[patLen] = (uint8_t)strtoul(hex, NULL, 16);
            patMask[patLen] = true;
            patLen++;
            p += 2;
        }
    }

    // Get module bounds
    HMODULE hMod = GetModuleHandleA(NULL);
    if (!hMod) return NULL;

    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)hMod;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((uint8_t*)hMod + dos->e_lfanew);
    uint8_t* base = (uint8_t*)hMod;
    DWORD size = nt->OptionalHeader.SizeOfImage;

    // Scan
    for (DWORD i = 0; i <= size - patLen; i++) {
        bool found = true;
        for (int j = 0; j < patLen; j++) {
            if (patMask[j] && base[i + j] != patBytes[j]) {
                found = false;
                break;
            }
        }
        if (found)
            return base + i;
    }

    return NULL;
}

// Read a 32-bit absolute address from pattern match + offset
static uint32_t SigRead32(uint8_t* match, int offset)
{
    if (!match) return 0;
    return *(uint32_t*)(match + offset);
}

// Resolve a relative E8 CALL at match+offset to its destination
static uint8_t* SigResolveCall(uint8_t* match, int offset)
{
    if (!match) return NULL;
    uint8_t* callAddr = match + offset;
    int32_t rel = *(int32_t*)(callAddr + 1);
    return callAddr + 5 + rel;
}
