#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/types.h>

#include <unordered_map>
#include <assert.h>
#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <libgen.h>

#include <unordered_map>

#define BUFFER_SIZE 1024
#define MAX_EVENT_NUMBER 1024
#define USERNAME_SIZE 64



struct fds {
  int epollfd;
  int sockfd;
};

struct client_info {
  char username[USERNAME_SIZE];
  char write_buf[BUFFER_SIZE];
  char read_buf[BUFFER_SIZE];
};

std::unordered_map<int, struct client_info> data;

int setnonblocking(int fd) {
  int old_option = fcntl(fd, F_GETFL);
  int new_option = old_option | O_NONBLOCK;
  fcntl(fd, F_SETFL, new_option);
  return old_option;
}

void addfd(int epollfd, int fd, bool oneshot) {
  epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;

  if (oneshot) {
    event.events |= EPOLLONESHOT;
  }

  epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
  setnonblocking(fd);
}

void delfd(int epollfd, int fd) {
  epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
  data.erase(fd);
}


void modfd(int epollfd, int fd, bool in2out) {
  epoll_event event;
  event.data.fd = fd;
  if (in2out) {
    event.events = EPOLLOUT | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
  }
  else {
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
  }
  epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
  setnonblocking(fd);
}

void reset_oneshot(int epollfd, int fd) {
  epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;

  epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void* recv_data(void* arg) {
  int connfd = ((fds*)arg)->sockfd;
  int epollfd = ((fds*)arg)->epollfd;
  
  //printf("getting data from %d\n", connfd);

  memset(data[connfd].read_buf, '\0', sizeof(data[connfd].read_buf));
  while (1) {
    int ret = recv(connfd, data[connfd].read_buf, BUFFER_SIZE-1, 0);
    if (ret == 0) {
      close(connfd);
      printf("%s closed the connection\n", data[connfd].username);
      data.erase(connfd);
      delfd(epollfd, connfd);
      break;
    }
    else if (ret < 0) {
      if (errno == EAGAIN) {
        reset_oneshot(epollfd, connfd);
        break;
      }
    }
    else {
      char * buf = data[connfd].read_buf;
      buf[strcspn(buf, "\n")] = 0;

      printf("%s : %s\n",data[connfd].username, buf);

      for (auto fd : data) {
        if (fd.first == connfd) continue;
        modfd(epollfd, fd.first, true);
        strcat(data[fd.first].write_buf, buf);
      }
      memset(buf, '\0', BUFFER_SIZE);
    }
  }
}

void* send_data(void* arg) {
  int connfd = ((fds*)arg)->sockfd;
  int epollfd = ((fds*)arg)->epollfd;

  //printf("sending data '%s' to %d\n", data[connfd].write_buf, connfd);

  char send_tmp[BUFFER_SIZE];
  memset(send_tmp, '\0', BUFFER_SIZE);
  strcat(send_tmp, data[connfd].username);
  strcat(send_tmp, " : ");
  strcat(send_tmp, data[connfd].write_buf);

  int ret = send(connfd, send_tmp, strlen(send_tmp), 0);
  assert(ret != -1);
  memset(data[connfd].write_buf, '\0', BUFFER_SIZE);
  modfd(epollfd, connfd, false);
}


int main(int argc, char* argv[]) {
  if( argc <= 2 )
  {
    printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
    return 1;
  }
  const char* ip = argv[1];
  int port = atoi( argv[2] );

  int ret = 0;
  struct sockaddr_in address;
  bzero( &address, sizeof( address ) );
  address.sin_family = AF_INET;
  inet_pton( AF_INET, ip, &address.sin_addr );
  address.sin_port = htons( port );

  int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
  assert( listenfd >= 0 );

  ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
  assert( ret != -1 );

  ret = listen( listenfd, 5 );
  assert( ret != -1 );


  epoll_event events[MAX_EVENT_NUMBER];
  int epollfd = epoll_create(5);
  assert(epollfd >= 0);
  addfd(epollfd, listenfd, false);

  while (1) {
    int ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
    if (ret < 0) {
      printf("epoll failure\n");
      break;
    }

    for (int i=0; i<ret; ++i) {
      int sockfd = events[i].data.fd;
      if (sockfd == listenfd) {
        struct sockaddr_in client;
        socklen_t client_len = sizeof(client);
        int connfd = accept(listenfd, (struct sockaddr*)&client, &client_len);
        // get username from the client
        char username_tmp[USERNAME_SIZE];
        memset(username_tmp, '\0', USERNAME_SIZE);
        int ret = recv(connfd, username_tmp, USERNAME_SIZE-1, 0);
        assert(ret != -1);
        strcat(data[connfd].username, username_tmp);
        printf("A new user comes: %s\n", username_tmp);
        addfd(epollfd, connfd, true);
      }
      else if (events[i].events & EPOLLIN) {
        pthread_t thread;
        fds infds;
        infds.epollfd = epollfd;
        infds.sockfd = sockfd;
        pthread_create(&thread, NULL, recv_data, (void*)&infds);
      }
      else if (events[i].events & EPOLLOUT) {
        pthread_t thread;
        fds outfds;
        outfds.epollfd = epollfd;
        outfds.sockfd = sockfd;
        pthread_create(&thread, NULL, send_data, (void*)&outfds);
      }
      else {
        printf("something else happened\n");
      }
    }
  }
  close(listenfd);
  return EXIT_SUCCESS;
}

