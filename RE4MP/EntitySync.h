#pragma once
#include <Windows.h>
#include <stdint.h>
#include <string.h>

#define MAX_SYNC_ENTITIES 256
#define INTERP_DURATION_MS 50.0f
#define ENTITY_STALE_MS 2000
#define ROOM_SYNC_INTERVAL_MS 200

// Interpolation state for a single entity (partner or enemy)
struct InterpState {
    float startPos[3];
    float startRot[3];
    float targetPos[3];
    float targetRot[3];
    int16_t targetHP;
    DWORD lastUpdateTick;
    uint8_t entityId;     // cModel::id_100 for validation
    bool active;
    // Animation state (applied directly, not interpolated)
    uint8_t  routine;     // cModel::r_no_0_FC
    int32_t  animSeq;     // current animation sequence
    float    animFrame;   // current frame in animation
    float    animSpeed;   // playback speed multiplier
    bool     hasAnim;     // true when animation data has been received
};

inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

inline float LerpAngle(float a, float b, float t) {
    float diff = b - a;
    while (diff > 3.14159265f) diff -= 6.28318530f;
    while (diff < -3.14159265f) diff += 6.28318530f;
    return a + diff * t;
}

inline void InterpState_Reset(InterpState* s) {
    memset(s, 0, sizeof(*s));
    s->targetHP = -1;
    s->active = false;
    s->hasAnim = false;
    s->animSeq = -1;
    s->animFrame = 0.0f;
    s->animSpeed = 1.0f;
    s->routine = 0;
}

inline void InterpState_SetTarget(InterpState* s, float* pos, float* rot, int16_t hp, uint8_t entId) {
    if (s->active) {
        // Compute current interpolated position as new start
        DWORD elapsed = GetTickCount() - s->lastUpdateTick;
        float t = (float)elapsed / INTERP_DURATION_MS;
        if (t > 1.0f) t = 1.0f;
        for (int i = 0; i < 3; i++) {
            s->startPos[i] = Lerp(s->startPos[i], s->targetPos[i], t);
            s->startRot[i] = LerpAngle(s->startRot[i], s->targetRot[i], t);
        }
    } else {
        // First update: snap start to target (no lerp from origin)
        memcpy(s->startPos, pos, sizeof(float) * 3);
        memcpy(s->startRot, rot, sizeof(float) * 3);
    }

    memcpy(s->targetPos, pos, sizeof(float) * 3);
    memcpy(s->targetRot, rot, sizeof(float) * 3);
    s->targetHP = hp;
    s->entityId = entId;
    s->lastUpdateTick = GetTickCount();
    s->active = true;
}

inline void InterpState_SetAnim(InterpState* s, uint8_t routine, int32_t seq, float frame, float speed) {
    s->routine = routine;
    s->animSeq = seq;
    s->animFrame = frame;
    s->animSpeed = speed;
    s->hasAnim = true;
}

inline void InterpState_GetCurrent(InterpState* s, float* outPos, float* outRot) {
    if (!s->active) return;

    DWORD elapsed = GetTickCount() - s->lastUpdateTick;
    float t = (float)elapsed / INTERP_DURATION_MS;
    if (t > 1.0f) t = 1.0f;

    for (int i = 0; i < 3; i++) {
        outPos[i] = Lerp(s->startPos[i], s->targetPos[i], t);
        outRot[i] = LerpAngle(s->startRot[i], s->targetRot[i], t);
    }
}

inline bool InterpState_IsStale(InterpState* s) {
    if (!s->active) return true;
    return (GetTickCount() - s->lastUpdateTick) > ENTITY_STALE_MS;
}

// Aggregate sync state for all entities
struct EntitySyncState {
    InterpState partnerInterp;
    InterpState enemyInterp[MAX_SYNC_ENTITIES];

    // Room tracking
    uint16_t localRoom;
    uint16_t remoteRoom;
    bool remoteRoomKnown;
    DWORD lastRoomSyncSend;

    // Client-side: previous enemy HP for damage detection
    int16_t prevEnemyHP[MAX_SYNC_ENTITIES];
    bool prevEnemyHPValid[MAX_SYNC_ENTITIES];
};

inline void EntitySyncState_Init(EntitySyncState* state) {
    memset(state, 0, sizeof(*state));
    InterpState_Reset(&state->partnerInterp);
    for (int i = 0; i < MAX_SYNC_ENTITIES; i++) {
        InterpState_Reset(&state->enemyInterp[i]);
        state->prevEnemyHP[i] = -1;
        state->prevEnemyHPValid[i] = false;
    }
    state->remoteRoomKnown = false;
    state->lastRoomSyncSend = 0;
}

inline void EntitySyncState_OnRoomChange(EntitySyncState* state) {
    InterpState_Reset(&state->partnerInterp);
    for (int i = 0; i < MAX_SYNC_ENTITIES; i++) {
        InterpState_Reset(&state->enemyInterp[i]);
        state->prevEnemyHP[i] = -1;
        state->prevEnemyHPValid[i] = false;
    }
}

inline bool EntitySyncState_SameRoom(EntitySyncState* state) {
    return state->remoteRoomKnown && (state->localRoom == state->remoteRoom);
}
