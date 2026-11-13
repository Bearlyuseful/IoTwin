#define _DEFAULT_SOURCE

#include "socketcan.h"

#include <linux/can/raw.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

int socketcan_open(const char* interface_name) {
  int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (fd < 0) {
    return -1;
  }
  struct ifreq request;
  memset(&request, 0, sizeof(request));
  strncpy(request.ifr_name, interface_name, IFNAMSIZ - 1);
  if (ioctl(fd, SIOCGIFINDEX, &request) < 0) {
    close(fd);
    return -1;
  }
  struct sockaddr_can address;
  memset(&address, 0, sizeof(address));
  address.can_family = AF_CAN;
  address.can_ifindex = request.ifr_ifindex;
  if (bind(fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

bool socketcan_to_frame(const struct can_frame* in, CanFrame* out) {
  if (in->can_id & CAN_ERR_FLAG) {
    return false;
  }
  can_frame_clear(out);
  out->extended = (in->can_id & CAN_EFF_FLAG) != 0;
  out->rtr = (in->can_id & CAN_RTR_FLAG) != 0;
  out->id = in->can_id & (out->extended ? CAN_EFF_MASK : CAN_SFF_MASK);
  out->dlc = in->len > CAN_MAX_DLEN ? CAN_MAX_DLEN : in->len;
  if (!out->rtr) {
    memcpy(out->data, in->data, out->dlc);
  }
  return true;
}

bool socketcan_from_frame(const CanFrame* in, struct can_frame* out) {
  if (!can_frame_is_valid(in)) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  out->can_id = in->id;
  if (in->extended) {
    out->can_id |= CAN_EFF_FLAG;
  }
  if (in->rtr) {
    out->can_id |= CAN_RTR_FLAG;
  }
  out->len = in->dlc;
  if (!in->rtr) {
    memcpy(out->data, in->data, in->dlc);
  }
  return true;
}
