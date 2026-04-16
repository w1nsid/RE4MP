// RE4MP.cpp : Multiplayer mod for Resident Evil 4 (2005 PC)
// Sends local player position over UDP, receives remote player position for partner AI
//

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include "detours.h"
#include <iostream>
#include <stdio.h>
#include <share.h>
#include "RE4MP.h"
#include "hooks.h"
#include "Re4Detours.h"

#include "../version.h"

static FILE* g_logFile = NULL;

void DbgLog(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
    printf("%s", buf);
    if (g_logFile) {
        fputs(buf, g_logFile);
        fflush(g_logFile);
    }
}

DWORD WINAPI MainThread(LPVOID param) {

    // Open log file next to the DLL (shared read so we can tail it)
    char logPath[MAX_PATH];
    GetModuleFileNameA((HMODULE)param, logPath, MAX_PATH);
    char* lastSlash = strrchr(logPath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat_s(logPath, MAX_PATH, "re4mp.log");
    g_logFile = _fsopen(logPath, "w", _SH_DENYWR);

    // Allocate a debug console
    AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);

    DbgLog("========================================\n");
    DbgLog("  RE4MP Client v%s\n", RE4MP_VERSION);
    DbgLog("  Built: %s\n", RE4MP_BUILD_DATE);
    DbgLog("========================================\n");
    DbgLog("[RE4MP] DLL loaded, MainThread started\n");

    base_addr = (DWORD)GetModuleHandleA(0);
    DbgLog("[RE4MP] Base address: 0x%08X\n", base_addr);

    DbgLog("[RE4MP] Hooking game functions...\n");
    HookFunctions(base_addr);
    DbgLog("[RE4MP] HookFunctions done\n");

    DbgLog("[RE4MP] Installing detours...");
    DetourFunctions(base_addr);
    DbgLog(" addresses saved (hooks deferred until spawn)\n");

    // Load config from re4mp.ini next to the DLL
    LoadConfig(&g_config, g_hModule);
    g_isHost = g_config.isHost;
    DbgLog("[RE4MP] Config loaded: ServerIP=%s, ServerPort=%d, HostMode=%s\n",
           g_config.serverIP, g_config.serverPort, g_isHost ? "HOST" : "CLIENT");

    // Initialize networking
    if (!InitNetwork(&g_net, &g_config)) {
        DbgLog("[RE4MP] ERROR: Failed to initialize network\n");
    }
    else {
        DbgLog("[RE4MP] Network initialized, connected to %s:%d\n", g_config.serverIP, g_config.serverPort);
    }

    DbgLog("[RE4MP] Entering main loop — press F5 to start sync\n");
    bool f5Held = false;
    bool loopActive = false;

    // Entity sync state (static to avoid large stack allocation)
    static EntitySyncState syncState;
    EntitySyncState_Init(&syncState);

    uint16_t prevRoom = 0;
    bool firstRoomRead = true;
    uint32_t prevPauseFlags = 0;

    while (true) {

        // F5: Start the sync loop
        if (GetAsyncKeyState(VK_F5) & 0x8000) {
            if (!f5Held) {
                f5Held = true;
                if (!loopActive) {
                    loopActive = true;
                    DbgLog("[RE4MP] F5 pressed — sync loop activated\n");
                } else {
                    DbgLog("[RE4MP] F5 pressed — sync loop already active\n");
                }
            }
        } else {
            f5Held = false;
        }

        // Nothing else runs until F5 activates the loop
        if (!loopActive) {
            if (GetAsyncKeyState(VK_END) & 0x8000) {
                DbgLog("[RE4MP] END pressed — unloading\n");
                break;
            }
            Sleep(1);
            continue;
        }

        // Skip ALL game memory access while not in MainLoop.
        // During room transitions, entity pointers (player, Ashley, enemies)
        // are invalid — accessing them causes crashes.
        if (!IsGameStable()) {
            // Still process network recv so we don't lose packets
            if (g_net.initialized) {
                RE4MPPacket recvPkt;
                while (RecvPacket(&g_net, &recvPkt)) {
                    switch (recvPkt.type) {
                    case PKT_ROOM_SYNC:
                        syncState.remoteRoom = recvPkt.entityIndex;
                        syncState.remoteRoomKnown = true;
                        break;
                    case PKT_PAUSE_SYNC:
                    {
                        uint32_t remotePause = ((uint32_t)(uint16_t)recvPkt.hp << 16)
                                             | (uint32_t)recvPkt.entityIndex;
                        SetPauseFlags(remotePause);
                        prevPauseFlags = remotePause;
                        break;
                    }
                    default:
                        break; // Discard entity packets during transition
                    }
                }
            }
            Sleep(10);
            continue;
        }

        // ===== From here on, game is in MainLoop — all pointers are safe =====

        // Auto-acquire Ashley pointer if she became available after a room transition
        if (playerTwoReady && !SubCharPointer()) {
            DbgLog("[RE4MP] Ashley lost (room transition?) — waiting for reload\n");
            playerTwoReady = false;
            playerTwoPtr = nullptr;
        }
        if (!playerTwoReady) {
            int* ashley = SubCharPointer();
            if (ashley) {
                playerTwoPtr = ashley;
                if (!g_detoursAttached) {
                    AttachDetours();
                }
                playerTwoReady = true;
                DbgLog("[RE4MP] Ashley auto-acquired at 0x%p — partner ready!\n", ashley);
            }
        }

        // =====================================================
        //  Room tracking — detect transitions, reset sync state
        // =====================================================
        uint16_t currentRoom = GetLocalRoomId();
        syncState.localRoom = currentRoom;

        if (!firstRoomRead && currentRoom != prevRoom) {
            DbgLog("[RE4MP] Room changed: 0x%04X -> 0x%04X, resetting sync state\n", prevRoom, currentRoom);
            EntitySyncState_OnRoomChange(&syncState);
        }
        prevRoom = currentRoom;
        firstRoomRead = false;

        // Force Ashley to be present in every area (sets STA_SUB_ASHLEY flag each frame)
        // Must be after IsGameStable() — GLOBAL_WK fields are unreliable during transitions.
        ForceAshleyPresent();

        // Prevent enemy AI from targeting Ashley for pickup (fixes desync)
        // Must be after IsGameStable() — iterates entity array which is invalid during loads.
        DisableEnemyAshleyPickup();

        // =====================================================
        //  Network SEND
        // =====================================================
        if (g_net.initialized) {

            DWORD now = GetTickCount();

            // --- Room sync (throttled, informational — tells remote what room we're in) ---
            if (now - syncState.lastRoomSyncSend >= ROOM_SYNC_INTERVAL_MS) {
                RE4MPPacket roomPkt;
                memset(&roomPkt, 0, sizeof(roomPkt));
                roomPkt.type = PKT_ROOM_SYNC;
                roomPkt.entityIndex = currentRoom;
                SendPacket(&g_net, &roomPkt);
                syncState.lastRoomSyncSend = now;
            }

            // --- Pause sync (send when changed) ---
            uint32_t curPauseFlags = GetPauseFlags();
            if (curPauseFlags != prevPauseFlags) {
                RE4MPPacket pausePkt;
                memset(&pausePkt, 0, sizeof(pausePkt));
                pausePkt.type = PKT_PAUSE_SYNC;
                pausePkt.entityIndex = (uint16_t)(curPauseFlags & 0xFFFF);
                pausePkt.hp = (int16_t)((curPauseFlags >> 16) & 0xFFFF);
                SendPacket(&g_net, &pausePkt);
                prevPauseFlags = curPauseFlags;
            }

            // --- Leon position (our player) ---
            if (playerTwoReady) {
                int* playerPtr = PlayerPointer();
                if (playerPtr) {
                    float* myPos = GetPlayerPosition();
                    float* myRot = GetCEmRot(playerPtr);
                    if (myPos && myRot) {
                        RE4MPPacket sendPkt;
                        memset(&sendPkt, 0, sizeof(sendPkt));
                        sendPkt.type = PKT_POSITION;
                        sendPkt.entityType = ENT_PLAYER;
                        sendPkt.entityIndex = 0;
                        sendPkt.pos[0] = myPos[0]; sendPkt.pos[1] = myPos[1]; sendPkt.pos[2] = myPos[2];
                        sendPkt.rot[0] = myRot[0]; sendPkt.rot[1] = myRot[1]; sendPkt.rot[2] = myRot[2];
                        sendPkt.hp = GetCEmHP(playerPtr);
                        sendPkt.routine = GetCEmRoutine(playerPtr);
                        sendPkt.animSeq = GetCEmAnimSeq(playerPtr);
                        sendPkt.animFrame = GetCEmAnimFrame(playerPtr);
                        sendPkt.animSpeed = GetCEmAnimSpeed(playerPtr);
                        SendPacket(&g_net, &sendPkt);
                    }
                }

                // --- Ashley state (include HP now) ---
                int* ashley = SubCharPointer();
                if (ashley) {
                    __try {
                        float* ashPos = GetCEmPos(ashley);
                        float* ashRot = GetCEmRot(ashley);
                        RE4MPPacket ashPkt;
                        memset(&ashPkt, 0, sizeof(ashPkt));
                        ashPkt.type = PKT_ENTITY_STATE;
                        ashPkt.entityType = ENT_PARTNER;
                        ashPkt.entityIndex = 0;
                        ashPkt.pos[0] = ashPos[0]; ashPkt.pos[1] = ashPos[1]; ashPkt.pos[2] = ashPos[2];
                        ashPkt.rot[0] = ashRot[0]; ashPkt.rot[1] = ashRot[1]; ashPkt.rot[2] = ashRot[2];
                        ashPkt.hp = GetCEmHP(ashley);
                        ashPkt.routine = GetCEmRoutine(ashley);
                        ashPkt.animSeq = GetCEmAnimSeq(ashley);
                        ashPkt.animFrame = GetCEmAnimFrame(ashley);
                        ashPkt.animSpeed = GetCEmAnimSpeed(ashley);
                        SendPacket(&g_net, &ashPkt);
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                }
            }

            // --- Enemy/boss states (HOST authoritative — only host sends position/anim) ---
            // Client only sends HP damage reports (PKT_HP_DAMAGE / PKT_ENTITY_DEATH).
            // Batched into fewer UDP datagrams to reduce syscall + header overhead.
            static int entitySendFrame = 0;
            entitySendFrame++;
            if (g_isHost && EntitySyncState_SameRoom(&syncState)) {
                RE4MPBatchPacket batch;
                batch.count = 0;

                uint32_t emCount = GetEmCount();
                for (uint32_t i = 0; i < emCount && i < MAX_SYNC_ENTITIES; i++) {
                    int* em = GetEmByIndex(i);
                    if (!em || !IsCEmValid(em)) continue;

                    uint8_t id = GetCEmId(em);
                    bool isBoss = IsBossId(id);
                    bool isEnemy = IsEnemyId(id);

                    if (!isEnemy && !isBoss) continue;

                    // Bosses send every frame, enemies every 3rd frame
                    if (!isBoss && (entitySendFrame % 3) != 0) continue;

                    __try {
                        float* emPos = GetCEmPos(em);
                        float* emRot = GetCEmRot(em);
                        RE4MPPacket* emPkt = &batch.entries[batch.count];
                        memset(emPkt, 0, sizeof(RE4MPPacket));
                        emPkt->type = PKT_ENTITY_STATE;
                        emPkt->entityType = isBoss ? ENT_BOSS : ENT_ENEMY;
                        emPkt->entityIndex = (uint16_t)i;
                        emPkt->pos[0] = emPos[0]; emPkt->pos[1] = emPos[1]; emPkt->pos[2] = emPos[2];
                        emPkt->rot[0] = emRot[0]; emPkt->rot[1] = emRot[1]; emPkt->rot[2] = emRot[2];
                        emPkt->hp = GetCEmHP(em);
                        emPkt->routine = GetCEmRoutine(em);
                        emPkt->animSeq = GetCEmAnimSeq(em);
                        emPkt->animFrame = GetCEmAnimFrame(em);
                        emPkt->animSpeed = GetCEmAnimSpeed(em);
                        batch.count++;

                        if (batch.count >= MAX_BATCH_ENTITIES) {
                            SendBatchPacket(&g_net, &batch);
                            batch.count = 0;
                        }
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                }

                if (batch.count > 0) {
                    SendBatchPacket(&g_net, &batch);
                }
            }

            // --- Detect local enemy HP changes and report to remote ---
            if (EntitySyncState_SameRoom(&syncState)) {
                uint32_t emCount = GetEmCount();
                for (uint32_t i = 0; i < emCount && i < MAX_SYNC_ENTITIES; i++) {
                    int* em = GetEmByIndex(i);
                    if (!em || !IsCEmValid(em)) {
                        syncState.prevEnemyHPValid[i] = false;
                        continue;
                    }

                    uint8_t id = GetCEmId(em);
                    if (!IsEnemyId(id) && !IsBossId(id)) continue;

                    __try {
                        int16_t currentHP = GetCEmHP(em);

                        if (syncState.prevEnemyHPValid[i]) {
                            if (currentHP < syncState.prevEnemyHP[i] && currentHP >= 0) {
                                RE4MPPacket dmgPkt;
                                memset(&dmgPkt, 0, sizeof(dmgPkt));
                                dmgPkt.type = PKT_HP_DAMAGE;
                                dmgPkt.entityType = IsBossId(id) ? ENT_BOSS : ENT_ENEMY;
                                dmgPkt.entityIndex = (uint16_t)i;
                                dmgPkt.hp = currentHP;
                                // Include routine + anim so remote sees damage/die animation
                                dmgPkt.routine = GetCEmRoutine(em);
                                dmgPkt.animSeq = GetCEmAnimSeq(em);
                                dmgPkt.animFrame = GetCEmAnimFrame(em);
                                dmgPkt.animSpeed = GetCEmAnimSpeed(em);
                                SendPacket(&g_net, &dmgPkt);
                            }

                            // Enemy died locally → notify remote
                            if (currentHP <= 0 && syncState.prevEnemyHP[i] > 0) {
                                RE4MPPacket deathPkt;
                                memset(&deathPkt, 0, sizeof(deathPkt));
                                deathPkt.type = PKT_ENTITY_DEATH;
                                deathPkt.entityType = IsBossId(id) ? ENT_BOSS : ENT_ENEMY;
                                deathPkt.entityIndex = (uint16_t)i;
                                deathPkt.hp = 0;
                                deathPkt.routine = GetCEmRoutine(em);
                                deathPkt.animSeq = GetCEmAnimSeq(em);
                                deathPkt.animFrame = GetCEmAnimFrame(em);
                                deathPkt.animSpeed = GetCEmAnimSpeed(em);
                                SendPacket(&g_net, &deathPkt);
                            }
                        }

                        syncState.prevEnemyHP[i] = currentHP;
                        syncState.prevEnemyHPValid[i] = true;
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                }
            }
        }

        // =====================================================
        //  Network RECV — process all queued packets
        // =====================================================
        if (g_net.initialized) {
            RE4MPPacket recvPkt;
            while (RecvPacket(&g_net, &recvPkt)) {
                switch (recvPkt.type) {
                case PKT_ROOM_SYNC:
                    syncState.remoteRoom = recvPkt.entityIndex;
                    if (!syncState.remoteRoomKnown) {
                        DbgLog("[RE4MP] Remote room received: 0x%04X\n", syncState.remoteRoom);
                    }
                    syncState.remoteRoomKnown = true;
                    break;

                case PKT_PAUSE_SYNC:
                {
                    uint32_t remotePause = ((uint32_t)(uint16_t)recvPkt.hp << 16)
                                         | (uint32_t)recvPkt.entityIndex;
                    SetPauseFlags(remotePause);
                    prevPauseFlags = remotePause;  // don't echo back
                    break;
                }

                case PKT_POSITION:
                    // Remote player's Leon position → interpolate onto our Ashley
                    if (EntitySyncState_SameRoom(&syncState)) {
                        InterpState_SetTarget(&syncState.partnerInterp,
                            recvPkt.pos, recvPkt.rot, recvPkt.hp, 0);
                        InterpState_SetAnim(&syncState.partnerInterp,
                            recvPkt.routine, recvPkt.animSeq, recvPkt.animFrame, recvPkt.animSpeed);
                        playerTwoPos[0] = recvPkt.pos[0];
                        playerTwoPos[1] = recvPkt.pos[1];
                        playerTwoPos[2] = recvPkt.pos[2];
                    }
                    break;

                case PKT_ENTITY_STATE:
                    if (recvPkt.entityType == ENT_PARTNER) {
                        remoteAshleyPos[0] = recvPkt.pos[0];
                        remoteAshleyPos[1] = recvPkt.pos[1];
                        remoteAshleyPos[2] = recvPkt.pos[2];
                    }
                    else if ((recvPkt.entityType == ENT_ENEMY || recvPkt.entityType == ENT_BOSS)
                             && !g_isHost && EntitySyncState_SameRoom(&syncState)) {
                        // Client receives authoritative enemy position/anim from host
                        uint32_t idx = recvPkt.entityIndex;
                        if (idx < MAX_SYNC_ENTITIES && idx < GetEmCount()) {
                            int* em = GetEmByIndex(idx);
                            if (em && IsCEmValid(em)) {
                                uint8_t localId = GetCEmId(em);
                                if (IsEnemyId(localId) || IsBossId(localId)) {
                                    InterpState_SetTarget(&syncState.enemyInterp[idx],
                                        recvPkt.pos, recvPkt.rot, recvPkt.hp, localId);
                                    InterpState_SetAnim(&syncState.enemyInterp[idx],
                                        recvPkt.routine, recvPkt.animSeq, recvPkt.animFrame, recvPkt.animSpeed);
                                }
                            }
                        }
                    }
                    break;

                case PKT_ENTITY_DEATH:
                    if (EntitySyncState_SameRoom(&syncState)) {
                        uint32_t idx = recvPkt.entityIndex;
                        if (idx < GetEmCount()) {
                            int* em = GetEmByIndex(idx);
                            if (em && IsCEmValid(em)) {
                                __try {
                                    SetCEmHP(em, 0);
                                    // Don't write routine/anim — the game engine will
                                    // naturally transition to Die when it sees HP<=0.
                                    // Writing routine directly corrupts the engine's
                                    // internal state machine and causes crashes.
                                } __except(EXCEPTION_EXECUTE_HANDLER) {}
                            }
                        }
                    }
                    break;

                case PKT_HP_DAMAGE:
                    if (EntitySyncState_SameRoom(&syncState)) {
                        uint32_t idx = recvPkt.entityIndex;
                        if (idx < GetEmCount()) {
                            int* em = GetEmByIndex(idx);
                            if (em && IsCEmValid(em)) {
                                __try {
                                    int16_t localHP = GetCEmHP(em);
                                    if (recvPkt.hp < localHP && recvPkt.hp >= 0) {
                                        SetCEmHP(em, recvPkt.hp);
                                        // Don't write routine/anim — the game engine will
                                        // naturally play the Damage/Die animation when it
                                        // detects the HP drop. For host-authoritative enemies,
                                        // the host's PKT_ENTITY_STATE will carry the correct
                                        // routine/anim to the client via interpolation.
                                    }
                                } __except(EXCEPTION_EXECUTE_HANDLER) {}
                            }
                        }
                    }
                    break;
                }
            }
        }

        // =====================================================
        //  Apply interpolation — smooth position writes
        // =====================================================

        // --- Partner (Ashley) interpolation ---
        if (playerTwoReady && playerTwoPtr && syncState.partnerInterp.active) {
            if (!InterpState_IsStale(&syncState.partnerInterp)) {
                float interpPos[3], interpRot[3];
                InterpState_GetCurrent(&syncState.partnerInterp, interpPos, interpRot);
                __try {
                    float* ashleyPos = GetCEmPos(playerTwoPtr);
                    float* ashleyRot = GetCEmRot(playerTwoPtr);

                    ashleyPos[0] = interpPos[0];
                    ashleyPos[1] = interpPos[1];
                    ashleyPos[2] = interpPos[2];
                    ashleyRot[0] = interpRot[0];
                    ashleyRot[1] = interpRot[1];
                    ashleyRot[2] = interpRot[2];

                    float* posOld = (float*)((DWORD)playerTwoPtr + 0x110);
                    posOld[0] = interpPos[0];
                    posOld[1] = interpPos[1];
                    posOld[2] = interpPos[2];

                    float* speed = (float*)((DWORD)playerTwoPtr + 0x104);
                    speed[0] = 0.0f;
                    speed[1] = 0.0f;
                    speed[2] = 0.0f;

                    // Sync remote player HP to Ashley
                    if (syncState.partnerInterp.targetHP >= 0) {
                        SetCEmHP(playerTwoPtr, syncState.partnerInterp.targetHP);
                    }

                    // Apply routine + animation state from remote player to Ashley
                    if (syncState.partnerInterp.hasAnim) {
                        uint8_t localRoutine = GetCEmRoutine(playerTwoPtr);
                        bool routineChanged = (localRoutine != syncState.partnerInterp.routine);

                        if (routineChanged) {
                            SetCEmRoutine(playerTwoPtr, syncState.partnerInterp.routine);
                        }

                        int32_t localSeq = GetCEmAnimSeq(playerTwoPtr);
                        if (routineChanged || localSeq != syncState.partnerInterp.animSeq) {
                            SetCEmAnimSeq(playerTwoPtr, syncState.partnerInterp.animSeq);
                            SetCEmAnimFrame(playerTwoPtr, syncState.partnerInterp.animFrame);
                        } else {
                            float localFrame = GetCEmAnimFrame(playerTwoPtr);
                            float frameDiff = syncState.partnerInterp.animFrame - localFrame;
                            if (frameDiff > 2.0f || frameDiff < -2.0f) {
                                SetCEmAnimFrame(playerTwoPtr, syncState.partnerInterp.animFrame);
                            }
                        }
                        SetCEmAnimSpeed(playerTwoPtr, syncState.partnerInterp.animSpeed);
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    playerTwoReady = false;
                    playerTwoPtr = nullptr;
                }
            }
        }

        // --- Enemy interpolation (CLIENT only — host is authoritative) ---
        // Host's game engine runs enemy AI natively. Client receives position
        // and HP from host. We do NOT write routine/animation — the engine's
        // internal state machine must manage those itself (writing r_no_0 from
        // outside causes crashes). The client's local AI will play animations
        // based on its own state, but position is corrected to match host.
        if (!g_isHost && EntitySyncState_SameRoom(&syncState)) {
            uint32_t emCount = GetEmCount();
            for (uint32_t i = 0; i < emCount && i < MAX_SYNC_ENTITIES; i++) {
                InterpState* es = &syncState.enemyInterp[i];
                if (!es->active || InterpState_IsStale(es)) continue;

                int* em = GetEmByIndex(i);
                if (!em || !IsCEmValid(em)) continue;

                __try {
                    // Interpolate position smoothly
                    float interpPos[3], interpRot[3];
                    InterpState_GetCurrent(es, interpPos, interpRot);

                    float* emPos = GetCEmPos(em);
                    float* emRot = GetCEmRot(em);
                    emPos[0] = interpPos[0]; emPos[1] = interpPos[1]; emPos[2] = interpPos[2];
                    emRot[0] = interpRot[0]; emRot[1] = interpRot[1]; emRot[2] = interpRot[2];

                    // Apply HP from host (use minimum so client damage also sticks)
                    if (es->targetHP >= 0) {
                        int16_t localHP = GetCEmHP(em);
                        if (es->targetHP < localHP) {
                            SetCEmHP(em, es->targetHP);
                        }
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
            }
        }

        // Exit
        if (GetAsyncKeyState(VK_END) & 0x8000) {
            DbgLog("[RE4MP] END pressed — unloading\n");
            break;
        }

        // Yield CPU briefly — Sleep(1) gives ~1ms resolution when timeBeginPeriod(1)
        // is active (which RE4 sets). This keeps the loop at ~500-1000 Hz instead of
        // the old 100 Hz, drastically reducing send/recv latency.
        Sleep(1);
    }

    DbgLog("[RE4MP] Cleaning up...\n");
    CleanupNetwork(&g_net);
    if (g_logFile) { fclose(g_logFile); g_logFile = NULL; }
    FreeConsole();
    FreeLibraryAndExitThread((HMODULE) param, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID l)
{
        switch (reason)
        {
        case DLL_PROCESS_ATTACH:
            g_hModule = hModule;
            CreateThread(NULL, NULL, MainThread, hModule, NULL, NULL);
            break;
        case DLL_PROCESS_DETACH:
        default:
            break;
        }

    return TRUE;
}

