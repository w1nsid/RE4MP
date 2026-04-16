#pragma once
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include "Config.h"

#pragma comment(lib, "ws2_32.lib")

// Packet types
#define PKT_POSITION      0  // Leon position + rotation
#define PKT_ENTITY_STATE  1  // Entity position + rotation (partner/enemy/boss)
#define PKT_ENTITY_DEATH  2  // Entity killed notification
#define PKT_ROOM_SYNC     3  // Room/area ID sync (entityIndex = roomId)
#define PKT_HP_DAMAGE     4  // Client->host: enemy damage report (hp = current HP)
#define PKT_MAX           4  // highest valid packet type

// Entity types (used in entityType field)
#define ENT_PLAYER   0  // Leon (the local player)
#define ENT_PARTNER  1  // Ashley / partner character
#define ENT_ENEMY    2  // Ganado / standard enemy
#define ENT_BOSS     3  // Boss entity

#pragma pack(push, 1)
struct RE4MPPacket {
    uint8_t  type;        // PKT_* packet type
    uint8_t  entityType;  // ENT_* entity classification
    uint16_t entityIndex; // emListIndex or array slot (identifies which entity)
    float    pos[3];      // x, y, z position (cCoord::pos_94)
    float    rot[3];      // pitch, yaw, roll rotation (cCoord::ang_A0)
    int16_t  hp;          // entity HP (cEm::hp_324), -1 if unused
    uint32_t seq;         // sequence number
    // Animation sync fields
    uint8_t  routine;     // cModel::r_no_0_FC (Init/Move/Damage/Die)
    int32_t  animSeq;     // MOTION_INFO::Seq (current animation sequence)
    float    animFrame;   // MOTION_INFO::Mot_frame (current frame)
    float    animSpeed;   // MOTION_INFO::Seq_speed (playback speed)
};

// Batched packet: multiple entities in one UDP datagram
#define MAX_BATCH_ENTITIES 32
struct RE4MPBatchPacket {
    uint8_t  count;       // number of entity entries (1..MAX_BATCH_ENTITIES)
    RE4MPPacket entries[MAX_BATCH_ENTITIES];
};
#pragma pack(pop)

struct RE4MPNetwork {
    SOCKET       sock;
    sockaddr_in  serverAddr;
    bool         initialized;
    uint32_t     sendSeq;
    uint32_t     lastRecvSeq;
};

inline bool InitNetwork(RE4MPNetwork* net, RE4MPConfig* config)
{
    memset(net, 0, sizeof(RE4MPNetwork));
    net->sock = INVALID_SOCKET;
    net->initialized = false;
    net->sendSeq = 0;
    net->lastRecvSeq = 0;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return false;

    net->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (net->sock == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    // Set non-blocking
    u_long mode = 1;
    ioctlsocket(net->sock, FIONBIO, &mode);

    // Resolve server address (supports both IP and DNS hostname)
    memset(&net->serverAddr, 0, sizeof(net->serverAddr));
    net->serverAddr.sin_family = AF_INET;
    net->serverAddr.sin_port = htons((u_short)config->serverPort);

    // Try as IP first, fall back to DNS lookup
    if (inet_pton(AF_INET, config->serverIP, &net->serverAddr.sin_addr) != 1) {
        struct addrinfo hints, *result = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;

        if (getaddrinfo(config->serverIP, NULL, &hints, &result) != 0 || result == NULL) {
            closesocket(net->sock);
            WSACleanup();
            return false;
        }
        net->serverAddr.sin_addr = ((sockaddr_in*)result->ai_addr)->sin_addr;
        freeaddrinfo(result);
    }

    net->initialized = true;

    // Enlarge socket buffers to handle packet bursts
    int bufSize = 65536;
    setsockopt(net->sock, SOL_SOCKET, SO_SNDBUF, (const char*)&bufSize, sizeof(bufSize));
    setsockopt(net->sock, SOL_SOCKET, SO_RCVBUF, (const char*)&bufSize, sizeof(bufSize));

    return true;
}

inline void SendPacket(RE4MPNetwork* net, RE4MPPacket* pkt)
{
    if (!net->initialized)
        return;

    pkt->seq = ++net->sendSeq;
    sendto(net->sock, (const char*)pkt, sizeof(RE4MPPacket), 0,
           (sockaddr*)&net->serverAddr, sizeof(net->serverAddr));
}

inline void SendBatchPacket(RE4MPNetwork* net, RE4MPBatchPacket* batch)
{
    if (!net->initialized || batch->count == 0)
        return;

    // Assign sequence numbers to all entries
    for (uint8_t i = 0; i < batch->count; i++) {
        batch->entries[i].seq = ++net->sendSeq;
    }

    // Send only the used portion: 1 byte count + count * sizeof(RE4MPPacket)
    int sendSize = 1 + batch->count * (int)sizeof(RE4MPPacket);
    sendto(net->sock, (const char*)batch, sendSize, 0,
           (sockaddr*)&net->serverAddr, sizeof(net->serverAddr));
}

// --- Batch receive queue (file-level statics) ---
static RE4MPPacket g_batchQueue[MAX_BATCH_ENTITIES];
static int g_batchQueueHead = 0;
static int g_batchQueueCount = 0;

// Returns true if a packet was received.
// Accepts all valid packets — no global sequence filtering.
// Different entity types send at different rates so a single sequence
// counter was incorrectly dropping valid packets from other streams.
inline bool RecvPacket(RE4MPNetwork* net, RE4MPPacket* pkt)
{
    if (!net->initialized)
        return false;

    // First, drain any remaining entries from a previous batch
    if (g_batchQueueHead < g_batchQueueCount) {
        memcpy(pkt, &g_batchQueue[g_batchQueueHead++], sizeof(RE4MPPacket));
        return true;
    }

    sockaddr_in fromAddr;
    int fromLen = sizeof(fromAddr);

    static uint8_t recvBuf[sizeof(RE4MPBatchPacket)];
    int result = recvfrom(net->sock, (char*)recvBuf, sizeof(recvBuf), 0,
                          (sockaddr*)&fromAddr, &fromLen);

    if (result <= 0)
        return false;

    // Single packet
    if (result == sizeof(RE4MPPacket)) {
        memcpy(pkt, recvBuf, sizeof(RE4MPPacket));
        if (pkt->type > PKT_MAX)
            return false;
        return true;
    }

    // Batch packet: first byte is count, remainder is array of RE4MPPacket
    if (result > 1) {
        uint8_t count = recvBuf[0];
        int expectedSize = 1 + count * (int)sizeof(RE4MPPacket);
        if (count > 0 && count <= MAX_BATCH_ENTITIES && result == expectedSize) {
            RE4MPPacket* entries = (RE4MPPacket*)(recvBuf + 1);

            // Queue entries [1..count-1] for subsequent RecvPacket calls
            g_batchQueueCount = 0;
            g_batchQueueHead = 0;
            for (uint8_t i = 1; i < count; i++) {
                if (entries[i].type <= PKT_MAX) {
                    g_batchQueue[g_batchQueueCount++] = entries[i];
                }
            }

            // Return entries[0] immediately
            if (entries[0].type <= PKT_MAX) {
                memcpy(pkt, &entries[0], sizeof(RE4MPPacket));
                return true;
            }
        }
    }

    return false;
}

inline void CleanupNetwork(RE4MPNetwork* net)
{
    if (net->initialized) {
        if (net->sock != INVALID_SOCKET) {
            closesocket(net->sock);
            net->sock = INVALID_SOCKET;
        }
        WSACleanup();
        net->initialized = false;
    }
}
