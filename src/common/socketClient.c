#include "socketClient.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

/**
 * @brief Function to generate a client socket
 *
 * @param ip IP-Address of the server to connect to 
 * @param port Port on the server to connect to 
 * @return Status from socket creation
 */
int create_socket(const char* ip, uint16_t port){
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if(sock == -1){
    perror("Socket creation failed.");
    return -1;
  }

  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  inet_pton(AF_INET, ip, &address.sin_addr);

  if(connect(sock, (struct sockaddr*)&address, sizeof(address)) < 0){
    perror("Connection failed!");
    close(sock);
    return -1;
  }
  return sock;
}


