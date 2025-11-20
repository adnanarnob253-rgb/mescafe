// Windows TCP chat server using WinSock2
// Build: gcc -std=c17 -O2 -o chat_server chat_server.c -lws2_32
// Usage: chat_server <port>
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "Ws2_32.lib")

#define MAX_CLIENTS  FD_SETSIZE
#define MAX_LINE     1024
#define MAX_NAME     32

typedef struct {
    SOCKET sock;
    char   name[MAX_NAME];
    int    active;
} client_t;

static client_t clients[MAX_CLIENTS];

static void broadcast(SOCKET except, const char* line) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].sock != except) {
            send(clients[i].sock, line, (int)strlen(line), 0);
        }
    }
}

static int find_client(SOCKET s) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].active && clients[i].sock == s) return i;
    return -1;
}

static int name_exists(const char* n) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].active && strcmp(clients[i].name, n) == 0) return 1;
    return 0;
}

static int add_client(SOCKET s, const char* n) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].active = 1;
            clients[i].sock = s;
            strncpy(clients[i].name, n, MAX_NAME - 1);
            clients[i].name[MAX_NAME - 1] = 0;
            return 1;
        }
    }
    return 0;
}

static void remove_client(SOCKET s) {
    int idx = find_client(s);
    if (idx >= 0) {
        char info[128];
        snprintf(info, sizeof(info), "INFO %s left\r\n", clients[idx].name);
        printf("[INFO] %s disconnected\n", clients[idx].name);
        broadcast(s, info);
        closesocket(s);
        clients[idx].active = 0;
    } else {
        closesocket(s);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    if (port <= 0) { fprintf(stderr, "Bad port\n"); return 1; }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        fprintf(stderr, "socket failed\n");
        WSACleanup();
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((u_short)port);

    if (bind(listenSock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "bind failed\n");
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }
    if (listen(listenSock, 8) == SOCKET_ERROR) {
        fprintf(stderr, "listen failed\n");
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }
    printf("[INFO] Server listening on %d\n", port);
    memset(clients,0,sizeof(clients));

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listenSock, &readfds);
        SOCKET maxfd = listenSock;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                FD_SET(clients[i].sock, &readfds);
                if (clients[i].sock > maxfd) maxfd = clients[i].sock;
            }
        }

        int rv = select((int)maxfd + 1, &readfds, NULL, NULL, NULL);
        if (rv == SOCKET_ERROR) {
            fprintf(stderr, "select error\n");
            break;
        }

        if (FD_ISSET(listenSock, &readfds)) {
            SOCKET s = accept(listenSock, NULL, NULL);
            if (s == INVALID_SOCKET) continue;
            printf("[INFO] New connection %llu\n", (unsigned long long)s);
            // No data yet; will process in next loop
            add_client(s, ""); // temp empty name until HELLO
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) continue;
            SOCKET s = clients[i].sock;
            if (!FD_ISSET(s, &readfds)) continue;
            char buf[MAX_LINE+2];
            int n = recv(s, buf, MAX_LINE, 0);
            if (n <= 0) {
                remove_client(s);
                continue;
            }
            buf[n] = 0;

            // Process lines (may contain multiple)
            char* ptr = buf;
            while (*ptr) {
                char* eol = strstr(ptr, "\n");
                if (!eol) break;
                *eol = 0;
                // Handle CR
                char* cr = strchr(ptr, '\r');
                if (cr) *cr = 0;

                if (clients[i].name[0] == 0) {
                    // Expect HELLO <name>
                    if (strncmp(ptr, "HELLO ", 6) == 0) {
                        const char* uname = ptr + 6;
                        if (strlen(uname) == 0 || strlen(uname) >= MAX_NAME || name_exists(uname)) {
                            send(s, "ERR bad_or_used_name\r\n", 22, 0);
                        } else {
                            strncpy(clients[i].name, uname, MAX_NAME-1);
                            clients[i].name[MAX_NAME-1] = 0;
                            send(s, "WELCOME\r\n", 9, 0);
                            char info[128];
                            snprintf(info, sizeof(info), "INFO %s joined\r\n", uname);
                            broadcast(s, info);
                            printf("[INFO] %s joined\n", uname);
                        }
                    } else {
                        send(s, "ERR expected_HELLO\r\n", 21, 0);
                    }
                } else {
                    if (strncmp(ptr, "SEND ", 5) == 0) {
                        const char* msg = ptr + 5;
                        if (*msg) {
                            char out[MAX_LINE + 64];
                            snprintf(out, sizeof(out), "MSG %s %s\r\n", clients[i].name, msg);
                            broadcast(s, out);
                            send(s, out, (int)strlen(out), 0);
                        }
                    } else if (strncmp(ptr, "QUIT", 4) == 0) {
                        remove_client(s);
                        break;
                    } else {
                        send(s, "ERR unknown\r\n", 13, 0);
                    }
                }
                ptr = eol + 1;
            }
        }
    }

    closesocket(listenSock);
    WSACleanup();
    return 0;
}
