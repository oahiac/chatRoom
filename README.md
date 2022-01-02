# chatRoom

multi-thread chat room based on epoll and poll

The program contains the server and the client.

## Server

The server is responsible for:
1. Getting new connections
2. Receving data from the clients
3. Distributing the message to other clients

In the main thread, I first use epoll to listen on `listenfd` to accept new connections.

When we got the new connection, add its file descriptor into the kernel event table `epollfd`

Also, use epoll to listen on the readable event `EPOLLIN` and writeable event `EPOLLOUT`

When the server gets the message from the client, it will create a child thread to handle the message:
1. read the tcp buffer, register `EPOLLOUT` event to the kernel event table.
2. write data to the write buffers of other connections

When the server gets the `EPOLLOUT` event, which means that the socket is ready to write, it will write the data from the write buffer to the socket to send it to the clients.

## Client

Use poll to listen on the fds. 

The client is responsible for:
- sending message to the server
- read server message

When the client detects `POLLIN` event from the stdin, use `splice` to redirect stdin to sockfd and send to the server.

When the client detects `POLLIN` event from the sockfd, read data from the server.
