#include "can_bus.h"
#include <pthread.h>
#include <stdio.h>
#include "canSimulator.h"
#include "../../common/socketClient.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>


int main() {
    printf("CAN Simulator Test\n");

    const char* server_ip = "127.0.0.1";
    uint16_t port = 8080;
    
    simulator_t sim;
    int socket_fd = create_socket(server_ip, port);

  if (socket_fd == -1) {
    fprintf(stderr, "Failed to connect to server\n");
    return EXIT_FAILURE;
  }

  printf("Connected to %s:%d\n", server_ip, port);

  sim.socket_fd = socket_fd;

  if(pthread_create(&sim.receive_thread, NULL, receive_thread, &sim) != 0) {
    perror("Failed to create receive Thread");
    close(socket_fd);
    return EXIT_FAILURE;
  }

  if(pthread_create(&sim.send_thread, NULL, send_thread, &sim) != 0) {
    perror("Failed to create send thread");
    pthread_cancel(sim.receive_thread);
    close(socket_fd);
    return EXIT_FAILURE;
  }


  printf("Threads started successfully\n");

  while(1){
    sleep(1);
  }

  pthread_join(sim.receive_thread, NULL);
  pthread_join(sim.send_thread, NULL);
  close(socket_fd);

  return EXIT_SUCCESS;

}
