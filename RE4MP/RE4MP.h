#pragma once
#include "Cache.h"

void HookFunctions(DWORD base_addr);
void CodeInjection(DWORD base_addr);

int* PlayerPointer();
float* GetPlayerPosition();
float* GetCEmPos(int* cEmAddr);
int* SubCharPointer();
void ForceAshleyPresent();