/* 1. read user input from stdin
 * 2. print data from the server to stdout
 */
#define _GNU_SOURCE 1 // poll
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>
#include <string.h>
#include <string>
#include <iostream>

#include <poll.h>
#include <fcntl.h>

#define BUFFER_SIZE 64

int main(int argc, char* argv[]) {
  if (argc <= 2) {
    fprintf(stderr, "usage: %s ip_address port_number\n", basename(argv[0]));
    return EXIT_FAILURE;
  }

  const char* ip = argv[1];
  int port = atoi(argv[2]);

  struct sockaddr_in server_address;
  bzero(&server_address, sizeof(server_address));
  server_address.sin_family = AF_INET;
  inet_pton(AF_INET, ip, &server_address.sin_addr);
  server_address.sin_port = htons(port);

  // enter username
  std::cout << "Enter username: ";
  std::string _username;
  std::cin >> _username;
  char username[50];
  strcpy(username, _username.c_str());


  int sockfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(sockfd >= 0);
  
  int ret = connect(sockfd, (struct sockaddr*)&server_address, sizeof(server_address));
  if (ret == -1) {
    fprintf(stderr, "connection failure\n");
    return EXIT_FAILURE;
  }
  // send username
  ret = send(sockfd, username, strlen(username), 0);
  assert(ret != -1);
  // use poll to listen on the server data and the stdin
  struct pollfd fds[2];
  fds[0].fd = 0; // stdin
  fds[0].events = POLLIN;
  fds[0].revents = 0;
  fds[1].fd = sockfd;
  fds[1].events = POLLIN | POLLRDHUP; // POLLRDHUP is the event where the server closed
  fds[1].revents = 0;

  char buffer[BUFFER_SIZE];
  int pipefd[2];
  ret = pipe(pipefd);
  assert(ret != -1);


  while (1) {
    int ret = poll(fds, 2, -1);
    assert(ret != -1);

    if (fds[0].revents & POLLIN) { // got user input
      // redirect to sockfd
      ret = splice(0, NULL, pipefd[1], NULL, 44343, SPLICE_F_MOVE | SPLICE_F_MORE);
      ret = splice(pipefd[0], NULL, sockfd, NULL, 44343, SPLICE_F_MOVE | SPLICE_F_MORE);
    }
    if (fds[1].revents & POLLIN) { // got server data
      // redirect to the stdout
      memset(buffer, '\0', BUFFER_SIZE);
      recv(fds[1].fd, buffer, BUFFER_SIZE-1, 0);
      printf("%s", buffer);
    }
    else if (fds[1].revents & POLLRDHUP) {
      // connection closed
      printf("connection closed\n");
      break;
    }
    else {
      continue;
    }
  }
  close(sockfd);
  return EXIT_SUCCESS;
}







