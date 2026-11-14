#ifndef IOTWIN_EL_GENERATE_H
#define IOTWIN_EL_GENERATE_H

/**
 * @file el_generate.h
 * @brief Synthetic installations of any size, for benchmarks and tests.
 *
 * A house with a three-phase supply (TN-S), RCD groups spread over the three
 * phases, final circuits with breakers (lighting B10 with 1.5 mm^2, sockets
 * B16 with 2.5 mm^2) and a chain of junction boxes per circuit. Lighting
 * boxes feed a switch and a lamp, socket boxes a socket with an appliance
 * plugged in now and then. The same seed gives the same house.
 *
 * Ids: intake, q<g> (RCD), f<c> (breaker), c<c>b<b> (box), s<c>b<b> (switch),
 * l<c>b<b> (lamp), k<c>b<b> (socket), a<c>b<b> (appliance), cable w<c>b<b>.
 */

#include <stdbool.h>

#include "elsim.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int circuits;         /**< final circuits, every second one lighting */
  int boxes;            /**< junction boxes per circuit */
  int circuits_per_rcd; /**< circuits behind one RCD */
  unsigned seed;        /**< cable lengths and appliances */
} ElHouseSpec;

/** @brief 24 circuits x 6 boxes, 6 circuits per RCD: about 1200 nodes. */
ElHouseSpec el_house_spec_default(void);

/** @brief Adds the house to an empty model. @return false if a device cannot be added. */
bool el_generate_house(ElModel* model, const ElHouseSpec* spec);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_EL_GENERATE_H */
