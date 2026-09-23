/* Compile-only variant check. Never included in ordinary firmware builds. */
#include "sdkconfig.h"
#undef CONFIG_GHOST_LED_GPIO
#define CONFIG_GHOST_LED_GPIO 2
#define CONFIG_GHOST_LED_ACTIVE_LOW 1
#define CONFIG_GHOST_TEST_PACKET_REPLAY 1
