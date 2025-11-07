#ifndef CAN_SIMULATOR_H
#define CAN_SIMULATOR_H

#include <pthread.h>
#include "../../common/socketClient.h"

void* receive_thread(void* arg);
void* send_thread(void* arg);


#endif // !CAN_SIMULATOR_H

