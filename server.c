#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <sys/epoll.h>
#include <fcntl.h>


#define BUFFER_SIZE     1024
#define PORT            3355
#define BACKLOG         5
#define MAX_EVENTS      1024

int setup_sock(int port)
{
    /*
        * This function sets up a socket listening on local port.
        *
        * port: port number to listen on.
        * :return: socket file descriptor.
        */
    int err;
    int listen_fd;
    struct sockaddr_in serv_addr;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        return -1;
    }


    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serv_addr.sin_port = htons(port);

    // before bind is called.
    // REUSEADDR.
    int opt_val = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR , &opt_val, sizeof opt_val) == -1) {
        perror("setsockopt");
    }

    err = bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if (err == -1) {
        perror("bind");
        exit(EXIT_FAILURE);        
    }


    err = listen(listen_fd, BACKLOG);
    if (err == -1) {
        perror("listen");
        return err;
    }

    return listen_fd;
}


void echo_request(int conn_fd)
{
    int n;
    time_t ticks;
    char sendBuff[BUFFER_SIZE];
    char recvBuff[BUFFER_SIZE];

    memset(sendBuff, 0, sizeof(sendBuff));
    memset(recvBuff, 0, sizeof(recvBuff));

    while(1) {
        n = recv(conn_fd, recvBuff, sizeof(recvBuff), 0);
        if (n > 0) {
            // 获取当前时间
            ticks = time(NULL);
            snprintf(sendBuff, sizeof(sendBuff), "%.24s: ", ctime(&ticks));

            recvBuff[n] = '\0';
            printf("received:  %s\n", recvBuff);
            strcat(sendBuff, recvBuff);
            send(conn_fd, sendBuff, strlen(sendBuff), 0);
            // avoiding block in this while loop.
            return;
        } else if (n == 0) {
            printf("client closed, close the connection.\n");
            break;
        } else {
            perror("recv:");
            break;
        }        
    }
    
    close(conn_fd);
}

int setnonblocking(int fd)
{
	int old_option = fcntl(fd, F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd, F_SETFL, new_option);
	return old_option;
}


int main(int argc, char *argv[])
{
    int listen_fd = 0, conn_fd = 0;
    int nfds, n;
    socklen_t cli_len;
    struct sockaddr_in cli_addr;
    cli_len = sizeof(cli_addr);

    printf("start server...\n");
    memset(&cli_addr, '0', sizeof(cli_addr));

    listen_fd = setup_sock(PORT);
    printf("listening on 0.0.0.0 %d...\n", PORT);


    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;

    int epollfd = epoll_create1(0);
    if (epollfd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    
    while(1) {
        // Specifying a timeout of -1 causes epoll_wait() to block indefinitely, 
        // while specifying a timeout equal to zero cause epoll_wait() to return immediately.
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (n = 0; n < nfds; ++n) {
            if (events[n].data.fd == listen_fd) {
                conn_fd = accept(listen_fd, (struct sockaddr *) &cli_addr, &cli_len);
                if (conn_fd == -1) {
                    perror("accept");
                    exit(EXIT_FAILURE);
                }
                printf("client ip: %s, port: %d\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));                
                setnonblocking(conn_fd);
                
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = conn_fd;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_fd, &ev) == -1) {
                    perror("epoll_ctl: conn_sock");
                    exit(EXIT_FAILURE);                
                }
            } else {
                echo_request(events[n].data.fd);
            }
        }
    }
}


