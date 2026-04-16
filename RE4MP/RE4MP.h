#pragma once
#include "Cache.h"

void HookFunctions(DWORD base_addr);

int* PlayerPointer();
float* GetPlayerPosition();
float* GetCEmPos(int* cEmAddr);
float* GetCEmRot(int* cEmAddr);
int16_t GetCEmHP(int* cEmAddr);
void SetCEmHP(int* cEmAddr, int16_t hp);
uint8_t GetCEmRoutine(int* cEmAddr);
void SetCEmRoutine(int* cEmAddr, uint8_t routine);
int32_t GetCEmAnimSeq(int* cEmAddr);
float GetCEmAnimFrame(int* cEmAddr);
float GetCEmAnimSpeed(int* cEmAddr);
void SetCEmAnimSeq(int* cEmAddr, int32_t seq);
void SetCEmAnimFrame(int* cEmAddr, float frame);
void SetCEmAnimSpeed(int* cEmAddr, float speed);
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
void DisableEnemyAshleyPickup();
uint16_t GetLocalRoomId();
uint32_t GetPauseFlags();
void SetPauseFlags(uint32_t flags);
bool IsGameInMainLoop();
bool IsGameStable();