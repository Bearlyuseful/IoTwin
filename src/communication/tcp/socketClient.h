#ifndef SOCKET_CLIENT_H
#define SOCKET_CLIENT_H

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

typedef struct {
  int socket_fd;
  pthread_t receive_thread;
  pthread_t send_thread;
  pthread_t logic_thread;
} simulator_t;

int create_socket(const char* ip, uint16_t port);
int connect_simulator(simulator_t* sim);




#endif

