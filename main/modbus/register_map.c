#include "protocol/protocol.h"

/* Runtime-only storage. The firmware intentionally contains no data-point map.
 * A complete uploaded mapping file populates these definitions in RAM. */
ghost_field_t ghost_fields[GHOST_FIELDS_MAX];
size_t ghost_field_count;
