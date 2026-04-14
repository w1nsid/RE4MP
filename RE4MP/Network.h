#pragma once
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include "Config.h"

#pragma comment(lib, "ws2_32.lib")

// Packet types
#define PKT_POSITION  0
#define PKT_SHOOT     1  // reserved
#define PKT_SPAWN     2  // reserved

#pragma pack(push, 1)
struct RE4MPPacket {
    uint8_t  type;
    float    pos[3];
    float    ang[3];
    uint32_t seq;
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

// Returns true if a packet was received
inline bool RecvPacket(RE4MPNetwork* net, RE4MPPacket* pkt)
{
    if (!net->initialized)
        return false;

    sockaddr_in fromAddr;
    int fromLen = sizeof(fromAddr);
    int result = recvfrom(net->sock, (char*)pkt, sizeof(RE4MPPacket), 0,
                          (sockaddr*)&fromAddr, &fromLen);

    if (result == sizeof(RE4MPPacket)) {
        // Validate packet type
        if (pkt->type > PKT_SPAWN)
            return false;

        // Drop stale out-of-order packets (but allow seq wraparound)
        if (pkt->seq <= net->lastRecvSeq && (net->lastRecvSeq - pkt->seq) < 1000) {
            return false;
        }
        net->lastRecvSeq = pkt->seq;

        return true;
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
