#ifndef F15_NET_COMMANDS_H
#define F15_NET_COMMANDS_H
/*
 * commands.h - semantic command <-> legacy BIOS word adapter (plan §3).
 */
#include <stdint.h>

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NetCmd -> BIOS key word for injection into the legacy dispatch. 0 = none. */
uint16_t netCmdToScan(uint8_t cmd);
/* BIOS key word -> NetCmd, or NC_NONE if the key has no wire equivalent
 * (local-only keys, unmapped keys). */
uint8_t netScanToCmd(uint16_t scan);
int netCmdIsLocalOnly(uint8_t cmd);

/* Fire-button held bits -> the axis-input slots keyDispatch feeds
 * (g_axisInputAccum[0]=gun, [1]=missile). */
static inline uint8_t netButtonsToAccum(uint8_t buttons) {
    return (uint8_t)((buttons & NB_GUN ? 1 : 0) | (buttons & NB_MISSILE ? 2 : 0));
}

#ifdef __cplusplus
}
#endif

#endif /* F15_NET_COMMANDS_H */
