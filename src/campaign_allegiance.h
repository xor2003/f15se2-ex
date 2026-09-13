#pragma once

#include "shared/common.h"

/* The final four flight slots are populated from the nearby base at runtime. */
static inline int campaignFriendlyAircraft(int flightSlot, int flightCount, int baseSlot) {
    const int baseSpawned = flightCount >= 4 && flightSlot >= flightCount - 4;
    return customCampaignAircraftFriendly(flightSlot, baseSpawned ? baseSlot : -1);
}
