// Exposes the unchanged vendor firmware table (gsl3680_firmware.h, see NOTICE)
// to the C++ driver. Compiled as C because the table's initialisers narrow
// int to char, which C++ rejects.
#include <stddef.h>
#include "gsl3680_firmware.h"

const struct fw_data *const gsl3680_firmware_table = GSLX680_FW;
const size_t gsl3680_firmware_count = sizeof(GSLX680_FW) / sizeof(GSLX680_FW[0]);
