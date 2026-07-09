#pragma once

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>

#define MAX_MEM_BUFFER_SIZE 8192
#define PORT_DEFAULT 8765

inline int connect_database(const char *unix_socket_path, const char *server_host, int server_port) {
    if (unix_socket_path != nullptr) {
        int sockfd = socket(PF_UNIX, SOCK_STREAM, 0);
        if (sockfd < 0) {
            fprintf(stderr, "failed to create unix socket. %s", strerror(errno));
            return -1;
        }
        struct sockaddr_un sockaddr;
        memset(&sockaddr, 0, sizeof(sockaddr));
        sockaddr.sun_family = PF_UNIX;
        snprintf(sockaddr.sun_path, sizeof(sockaddr.sun_path), "%s", unix_socket_path);
        if (connect(sockfd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0) {
            fprintf(stderr, "failed to connect to server. unix socket path '%s'. error %s", sockaddr.sun_path, strerror(errno));
            close(sockfd);
            return -1;
        }
        return sockfd;
    } else {
        struct hostent *host;
        struct sockaddr_in serv_addr;
        if ((host = gethostbyname(server_host)) == NULL) {
            fprintf(stderr, "gethostbyname failed. errmsg=%d:%s\n", errno, strerror(errno));
            return -1;
        }
        int sockfd;
        if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            fprintf(stderr, "create socket error. errmsg=%d:%s\n", errno, strerror(errno));
            return -1;
        }
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(server_port);
        serv_addr.sin_addr = *((struct in_addr *)host->h_addr);
        bzero(&(serv_addr.sin_zero), 8);
        if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr)) == -1) {
            fprintf(stderr, "Failed to connect. errmsg=%d:%s\n", errno, strerror(errno));
            close(sockfd);
            return -1;
        }
        return sockfd;
    }
}

inline int send_sql(int sockfd, const std::string& sql) {
    char recv_buf[MAX_MEM_BUFFER_SIZE];
    int send_bytes;
    if ((send_bytes = write(sockfd, sql.c_str(), sql.length() + 1)) == -1) {
        return -1;
    }
    return send_bytes;
}

inline int send_recv_sql(int sockfd, const std::string& sql, char* recv_buf) {
    int send_bytes;
    if ((send_bytes = write(sockfd, sql.c_str(), sql.length() + 1)) == -1) {
        recv_buf[0] = '\0';
        return -1;
    }
    int len = recv(sockfd, recv_buf, MAX_MEM_BUFFER_SIZE, 0);
    if (len < 0) {
        fprintf(stderr, "Connection was broken: %s\n", strerror(errno));
        recv_buf[0] = '\0';
        return -1;
    } else if (len == 0) {
        recv_buf[0] = '\0';
        return 0;
    }
    recv_buf[len] = '\0';
    return len;
}

inline void disconnect(int sockfd) {
    close(sockfd);
}
