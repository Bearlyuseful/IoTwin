#include "canSimulator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

void* receive_thread(void* arg) {
  simulator_t* sim = (simulator_t*) arg;
  uint8_t buffer[16];

  while(1){
    uint8_t bytes_read = read(sim->socket_fd, buffer, sizeof(buffer));
    
    if(bytes_read > 0){
      for(uint8_t i = 0; i < bytes_read; i++) {
        printf("Received byte %d: %d\n", i, buffer[i]);
      }
    }
  }
}

void* send_thread(void* arg) {
  simulator_t* sim = (simulator_t*)arg;
  
  while(1) {
    char message[] = "CAN_MESSAGE\n";
    write(sim->socket_fd, message, strlen(message));
    sleep(1);
  }
}
