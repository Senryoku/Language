#include <stdio.h>

#include <string>

#ifdef __linux__
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#elif _WIN32
#pragma comment(lib, "Ws2_32.lib")

#include <winsock2.h>

#include <ws2tcpip.h> // inet_pton
#define NOMINMAX
#include <windows.h> // FormatMessageA
#endif

#include <string.h>

#if _WIN32
std::string get_error_string(int errorCode) {
    // Get the error message for the error code
    LPSTR  messageBuffer = nullptr;
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, errorCode,
                                 MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&messageBuffer), 0, nullptr);

    // Create a string with the error message
    std::string message(messageBuffer, size);

    // Free the buffer
    LocalFree(messageBuffer);

    return message;
}
#endif

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

void __socket_cleanup() {
#ifdef __linux__
#elif _WIN32
    WSACleanup();
#endif
}

int __socket_create() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
        auto err = get_socket_error();
        printf("Error getting socket (%d): %s.\n", err, get_error_string(err).c_str());
        return 0;
    }
    return sockfd;
}

int __socket_connect(int sockfd, const char* addr, int port) {
    printf("__socket_connect to %s:%d\n", addr, port);

    struct sockaddr server_addr;

    struct sockaddr_in server_addr_in;
    memset(&server_addr_in, 0, sizeof(server_addr_in));
    server_addr_in.sin_family = AF_INET;
    server_addr_in.sin_port = htons(port);

    if(auto inet_pton_result = inet_pton(AF_INET, addr, &server_addr_in.sin_addr); inet_pton_result <= 0) {
        if(inet_pton_result == 0) {
            printf("__socket_connect: '%s' is not a valid IPv4 or IPv6.\n", addr);
            struct addrinfo hints;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            struct addrinfo* results;
            char             port_str[6];
            snprintf(port_str, sizeof(port_str), "%d", port);
            int status = getaddrinfo(addr, port_str, &hints, &results);
            if(status != 0) {
                printf("__socket_connect: '%s' could not be resolved to an IP address using getaddrinfo (error: %d).\n", addr, status);
                freeaddrinfo(results);
                return 1;
            }
            server_addr = *results->ai_addr;
            freeaddrinfo(results);
        } else {
            printf("__socket_connect: inet_pton error %d.\n", inet_pton_result);
            return 1;
        }
    } else
        server_addr = *(struct sockaddr*)&server_addr_in;

    // Connect to the server
    if(connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        auto err = get_socket_error();
        printf("Error connecting to %s (%d): %s.\n", addr, err, get_error_string(err).c_str());
        return err;
    }

    return 0;
}

int __socket_send(int sockfd, const char* req) {
    //printf("__socket_send on socket %d\n", sockfd);

    const std::string request(req);
    return send(sockfd, request.c_str(), request.size(), 0);
}

size_t __socket_recv(int sockfd, char* buff, size_t buff_size) {
    //printf("__socket_recv on socket %d\n", sockfd);

    int   status = recv(sockfd, buff, buff_size, 0);
    if(status == SOCKET_ERROR) {
        auto err = get_socket_error();
        printf("__socket_recv: error (%d): %s\n", err, get_error_string(err).c_str());
        return 0;
    }
    //printf("__socket_recv: received \n-------------------------------------------\n%s\n-------------------------------------------\n", buff);

    return status;
}

int __socket_close(int sockfd) {
    // FIXME: Check return value.
    return closesocket(sockfd);
}

} // extern "C"