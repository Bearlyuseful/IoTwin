#include "can_bus.h"

int main(void) {
    can_init();
    can_send(0x101, 0x01);
    return 0;
}
