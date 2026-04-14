#pragma once
#include "Cache.h"

void HookFunctions(DWORD base_addr);
void CodeInjection(DWORD base_addr);

int* PlayerPointer();
float* GetPlayerPosition();
float* GetCEmPos(int* cEmAddr);
float* GetCEmRot(int* cEmAddr);
int16_t GetCEmHP(int* cEmAddr);
uint8_t GetCEmId(int* cEmAddr);
uint8_t GetCEmListIndex(int* cEmAddr);
bool IsCEmValid(int* cEmAddr);
bool IsEnemyId(uint8_t id);
bool IsBossId(uint8_t id);
uint32_t GetEmCount();
uint32_t GetEmBlockSize();
int* GetEmByIndex(uint32_t idx);
int* SubCharPointer();
void ForceAshleyPresent();