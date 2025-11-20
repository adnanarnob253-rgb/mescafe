// Windows TCP chat client using WinSock2
// Build: gcc -std=c17 -O2 -o chat_client chat_client.c -lws2_32
// Usage: chat_client <server_ip> <port> <username>
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "Ws2_32.lib")

#define MAX_LINE 1024

static void trim(char* s) {
    size_t n = strlen(s);
    while (n>0 && (s[n-1]=='\n'||s[n-1]=='\r')) { s[n-1]=0; n--; }
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_ip> <port> <username>\n", argv[0]);
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi(argv[2]);
    const char* uname = argv[3];

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa)!=0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s==INVALID_SOCKET) { fprintf(stderr,"socket fail\n"); WSACleanup(); return 1; }

    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(s,(struct sockaddr*)&addr,sizeof(addr))==SOCKET_ERROR){
        fprintf(stderr,"connect failed\n");
        closesocket(s); WSACleanup(); return 1;
    }

    char hello[MAX_LINE];
    snprintf(hello,sizeof(hello),"HELLO %s\r\n",uname);
    send(s, hello, (int)strlen(hello), 0);

    char resp[256];
    int n = recv(s, resp, sizeof(resp)-1, 0);
    if (n<=0) { fprintf(stderr,"server closed\n"); closesocket(s); WSACleanup(); return 1; }
    resp[n]=0;
    if (strncmp(resp,"WELCOME",7)!=0) {
        fprintf(stderr,"Handshake failed: %s\n", resp);
        closesocket(s); WSACleanup(); return 1;
    }
    printf("Connected. /quit to exit.\n");

    // Non-blocking input multiplex: simple select on socket + stdin (Windows: use select only on sockets; fallback naive loop)
    for (;;) {
        // Receive pending
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s,&rfds);
        struct timeval tv; tv.tv_sec=0; tv.tv_usec=100000;
        int rv = select(0,&rfds,NULL,NULL,&tv);
        if (rv>0 && FD_ISSET(s,&rfds)) {
            char buf[MAX_LINE+2];
            int rn = recv(s, buf, MAX_LINE, 0);
            if (rn<=0) { printf("Disconnected.\n"); break; }
            buf[rn]=0;
            // Print lines
            char* p=buf;
            while (*p) {
                char* e = strstr(p,"\n");
                if (!e) break;
                *e=0;
                char* cr=strchr(p,'\r'); if(cr) *cr=0;
                printf("%s\n", p);
                p = e+1;
            }
        }

        // Check stdin (simple polling)
        static char linebuf[MAX_LINE+1];
        static int haveLine = 0;
        if (!haveLine && fgets(linebuf, sizeof(linebuf), stdin)) {
            trim(linebuf);
            if (strcmp(linebuf,"/quit")==0) {
                send(s,"QUIT\r\n",6,0);
                break;
            }
            if (linebuf[0]) {
                char out[MAX_LINE+16];
                snprintf(out,sizeof(out),"SEND %s\r\n",linebuf);
                send(s,out,(int)strlen(out),0);
            }
            haveLine = 0;
        }
    }

    closesocket(s);
    WSACleanup();
    return 0;
}
