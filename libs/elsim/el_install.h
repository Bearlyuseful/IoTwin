#ifndef IOTWIN_EL_INSTALL_H
#define IOTWIN_EL_INSTALL_H

/**
 * @file el_install.h
 * @brief Loads an installation (a JSON file in data/installations) into an ElModel.
 *
 * Parameters of a device come from, in this order: the defaults of its type,
 * the component it refers to (data/components, by id), and fields of the
 * device itself. Parameter names are the ones of the component database:
 *
 *   rated_current, magnetic_trip_multiple, thermal_limit (breaker),
 *   melting_i2t (fuse), rated_residual_current (rcd), rated_power,
 *   rated_voltage (load), contact_resistance; supply: system (TN-S, TN-C-S,
 *   TT), voltage, frequency, loop_impedance, earth_electrode, source_earth.
 *
 * A breaker component may give its tripping characteristic as property
 * "characteristic": B (5 x In), C (10 x In) or D (20 x In).
 */

#include <stdbool.h>
#include <stddef.h>

#include "elsim.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @param components_dir Directory searched recursively for component files, may be NULL.
 * @return false with a message in @p error (file, device or connection that is wrong).
 */
bool el_load_installation(ElModel* model, const char* path, const char* components_dir, char* error,
                          size_t error_size);

/** @brief Same from a JSON text (tests); components as above. */
bool el_load_installation_text(ElModel* model, const char* text, const char* components_dir, char* error,
                               size_t error_size);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_EL_INSTALL_H */
