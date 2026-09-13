#pragma once

#include "shared/common.h"

static inline int svnFriendlyAircraft(int aircraftType) {
    if (!customWorldScenarioIs("SVN")) return 0;
    switch (aircraftType) {
    case 0: case 1: case 2: case 4: case 5: case 9:
    case 11: case 14: case 15: case 16:
        return 1;
    default:
        return 0;
    }
}
