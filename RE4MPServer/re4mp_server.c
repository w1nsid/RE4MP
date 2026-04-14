/*
 * RE4MP Dedicated Server
 * Relay server for RE4MP multiplayer mod.
 * Accepts exactly 2 clients and forwards each client's packets to the other.
 *
 * Compiles on Linux:   gcc -O2 -o re4mp-server re4mp_server.c
 * Compiles on Windows: cl.exe /O2 re4mp_server.c ws2_32.lib
 *
 * Usage: ./re4mp-server [port]
 *        Default port: 27015
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>

#include "../version.h"

#ifdef _WIN32
  #include <WinSock2.h>
  #include <WS2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef int socklen_t;
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <errno.h>
  #include <fcntl.h>
  typedef int SOCKET;
  #define INVALID_SOCKET -1
  #define SOCKET_ERROR   -1
  #define closesocket    close
#endif

/* Must match the client's packet layout exactly */
#pragma pack(push, 1)
struct RE4MPPacket {
    uint8_t  type;
    float    pos[3];
    float    ang[3];
    uint32_t seq;
};
#pragma pack(pop)

#define PKT_POSITION 0
#define PKT_SHOOT    1
#define PKT_SPAWN    2

#define MAX_CLIENTS        2
#define CLIENT_TIMEOUT_SEC 30

struct Client {
    struct sockaddr_in addr;
    int    active;
    time_t last_seen;
};

static volatile int g_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

static int addr_equal(const struct sockaddr_in* a, const struct sockaddr_in* b)
{
    return a->sin_addr.s_addr == b->sin_addr.s_addr
        && a->sin_port == b->sin_port;
}

int main(int argc, char* argv[])
{
    int port = 27015;
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[1]);
            return 1;
        }
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Failed to create socket\n");
        return 1;
    }

    /* Allow immediate rebind after restart */
    int opt = 1;
#ifdef _WIN32
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons((uint16_t)port);

    if (bind(sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "Failed to bind to port %d\n", port);
        closesocket(sock);
        return 1;
    }

    printf("========================================\n");
    printf("  RE4MP Server v%s\n", RE4MP_VERSION);
    printf("  Built: %s\n", RE4MP_BUILD_DATE);
    printf("========================================\n");
    printf("RE4MP Server listening on UDP port %d\n", port);
    printf("Client timeout: %d seconds\n", CLIENT_TIMEOUT_SEC);
    printf("Waiting for 2 clients to connect...\n");
    fflush(stdout);

    struct Client clients[MAX_CLIENTS];
    memset(clients, 0, sizeof(clients));

    struct RE4MPPacket pkt;
    struct sockaddr_in from_addr;

    while (g_running) {
        /* Use select() so we can check timeouts even when no packets arrive */
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int sel = select((int)sock + 1, &readfds, NULL, NULL, &tv);

        /* Check for timed-out clients */
        time_t now = time(NULL);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active && difftime(now, clients[i].last_seen) > CLIENT_TIMEOUT_SEC) {
                char ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &clients[i].addr.sin_addr, ip_str, sizeof(ip_str));
                printf("Client %d timed out: %s:%d (no packets for %ds)\n",
                       i + 1, ip_str, ntohs(clients[i].addr.sin_port), CLIENT_TIMEOUT_SEC);
                fflush(stdout);
                clients[i].active = 0;
                memset(&clients[i].addr, 0, sizeof(clients[i].addr));
            }
        }

        if (sel <= 0) {
            /* Timeout or error — loop back to check again */
            continue;
        }

        socklen_t from_len = sizeof(from_addr);
        int received = recvfrom(sock, (char*)&pkt, sizeof(pkt), 0,
                                (struct sockaddr*)&from_addr, &from_len);

        if (received != (int)sizeof(pkt)) {
            /* Ignore malformed packets */
            continue;
        }

        /* Validate packet type */
        if (pkt.type > PKT_SPAWN) {
            continue;
        }

        /* Find which client slot this came from */
        int sender = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active && addr_equal(&clients[i].addr, &from_addr)) {
                sender = i;
                clients[i].last_seen = time(NULL);
                break;
            }
        }

        /* Register new client if we have a free slot */
        if (sender < 0) {
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (!clients[i].active) {
                    clients[i].addr = from_addr;
                    clients[i].active = 1;
                    clients[i].last_seen = time(NULL);
                    sender = i;

                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, sizeof(ip_str));
                    printf("Client %d connected: %s:%d\n",
                           i + 1, ip_str, ntohs(from_addr.sin_port));
                    fflush(stdout);

                    if (clients[0].active && clients[1].active) {
                        printf("Both clients connected. Relaying packets.\n");
                        fflush(stdout);
                    }
                    break;
                }
            }
        }

        /* Unknown sender and no free slots — drop */
        if (sender < 0) {
            continue;
        }

        /* Forward packet to the OTHER client */
        int other = 1 - sender;
        if (clients[other].active) {
            sendto(sock, (const char*)&pkt, sizeof(pkt), 0,
                   (struct sockaddr*)&clients[other].addr,
                   sizeof(clients[other].addr));
        }
    }

    printf("\nServer shutting down.\n");
    closesocket(sock);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
