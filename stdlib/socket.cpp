#include <stdio.h>

#include <string>

#ifdef __linux__
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>
#elif _WIN32
    #pragma comment(lib, "Ws2_32.lib")
    #include <winsock2.h>
#endif

#include <string.h>

extern "C" {

int get_socket_error() {
#ifdef __linux__
    return errno;
#elif _WIN32
    return WSAGetLastError();
#endif
}


int __socket_init() {
#ifdef __linux__
#elif _WIN32
    WORD    wVersionRequested = MAKEWORD(2, 2);
    WSADATA wsaData;
    auto    err = WSAStartup(wVersionRequested, &wsaData);
    if(err != 0) {
        printf("WSAStartup failed with error: %d\n", err);
        return err;
    }
#endif
    return 0;
}

int __socket_create() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
        // Handle error
        printf("Error getting socket: %d.\n", get_socket_error());
        return 0;
    }
    return sockfd;
}
 
int __socket_connect(int sockfd, const char* addr, int port) {
    printf("__socket_connect to %s:%d\n", addr, port);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(addr);

    // Connect to the server
    if(connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        auto err = get_socket_error();
        printf("Error connecting to %s: %d.\n", addr, err);
        return err;
    }

    return 0;
}

int __socket_send(int sockfd, const char* req) {
    printf("__socket_send on socket %d\n", sockfd);

    const std::string request(req);
    return send(sockfd, request.c_str(), request.size(), 0);
}

int __socket_close(int sockfd) {
    // FIXME: Check return value.
    return closesocket(sockfd);
}

int test_socket() {
    WORD  wVersionRequested = MAKEWORD(2, 2);
    WSADATA wsaData;
    auto err = WSAStartup(wVersionRequested, &wsaData);
    if(err != 0) {
        /* Tell the user that we could not find a usable */
        /* Winsock DLL.                                  */
        printf("WSAStartup failed with error: %d\n", err);
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
        // Handle error
        printf("Error getting socket: %d.\n", get_socket_error());
        return 0;
    }

    const char addr[] = "127.0.0.1";

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8000);
    server_addr.sin_addr.s_addr = inet_addr(addr);

    // Connect to the server
    if(connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Error connecting to %s: %d.\n", addr, get_socket_error());
        return 0;
    }

    const std::string request("GET / HTTP/1.0\r\n\r\n");
    send(sockfd, request.c_str(), request.size(), 0);

    closesocket(sockfd);

    return sockfd;
}

}