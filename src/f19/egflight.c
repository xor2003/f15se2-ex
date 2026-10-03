#include "f19eg.h"

/* egflight.c — F19 flight-model routines (seg000).
   Verified byte-identical against EGAME.EXE via tools/portcheck.py. */

/* ==== seg000:0x215c ==== */
struct CommData { int8 pad72[0x72]; int16 setupUseJoy; int8 pad74[4]; int16 gfxModeNum; };
struct ViewSnapshot { int32 worldX, worldY; int16 alt, heading, pitch, roll; };
struct SimObject { int16 objType; uint16 posX; int16 posY; int16 alt;
                   int32 worldX, worldY;
                   union { int16 w; uint8 b[2]; } heading;
                   int16 pitch; union { int16 w; uint8 b[2]; } bank;
                   int16 spec; union { uint16 w; uint8 b[2]; } flags;
                   int16 speed, timer, weaponType, terrainColor, damage; };
struct MapTarget { int16 objType; uint16 mapX; uint16 mapY; int16 active;
                   int16 flags; int16 alertLevel; int16 threatTimer; int16 symbol; };
struct Particle { int16 posX, posY, alt, spin; };
void far applyViewScaleMode(void);                 /* sub_2208E (seg002) */
void far initJoystickCalibration(void);            /* sub_22C5E (seg002) */
void far readCalibratedJoystick(void);             /* sub_22C7F (seg002) */
void far audio_setEnginePitch(int16, int16);       /* sub_2F23C (dseg stub) */
int16 fixedMulQ14(int16, int16);                   /* sub_11BC6 */
int16 isqrt(int16);                                /* sub_13387 */
int16 sine(int16);                                 /* sub_11C2D */
int16 cosine(int16);                               /* sub_11C1D */
int16 abs(int16);

int16 _bios_keybrd(int16 cmd);
char *itoa(int16 v, char *buf, int16 base);
char *strcpy(char *, const char *);
char *strcat(char *, const char *);
void makeSound(int16, int16);
void commitCommSnapshot(int16);                    /* sub_14C3C */
void waitFrameSync(int16);                         /* sub_104E2 */
void setDrawColor(int16);                          /* sub_18F9C */
void fillRectBoth(int16, int16, int16, int16);     /* sub_18FB2 */
void drawStatusItem(int16, int16);                 /* sub_19007 */
void blitSprite(int16, int16, int16, int16, int16, int16, int16); /* sub_19912 */
int16 clampRange(int16, int16, int16);             /* sub_1D1FA */
int16 clampValue(int16, int16, int16);             /* sub_1D223 */
int16 rangeApprox(int16, int16);                   /* sub_1D23B */
int16 sinMul(int16, int16);                        /* sub_1D3EC */
int16 cosMul(int16, int16);                        /* sub_1D404 */
int16 randomRange(int16);                          /* sub_1D46B */
void far gfx_copyRect(int16, int16, int16, int16, int16, int16, int16, int16); /* sub_2F0FC */
void rebuildOrientation(void);
void applyRotationDelta(const int16 *, const int16 *);
void computeAttitudeAngles(void);
void drawAirspeedTape(void);
void waitForKeyPress(void);

void stepFlightModel(void) {
    int16 p;        /* bp-0x02 */
    int16 a;        /* bp-0x04 */
    int16 b;        /* bp-0x06 */
    int16 c;        /* bp-0x08 */
    int16 d;        /* bp-0x0A */
    int16 lastAlt;  /* bp-0x0C: altitude before this frame's update */
    int16 v0;       /* bp-0x10: pitch-path temp */
    int16 v;        /* bp-0x0E */
    int16 yawAng;   /* bp-0x12: yaw rotation delta */
    int16 yawIn;    /* bp-0x14: yaw lead term */
    int16 z;        /* bp-0x1A */
    int16 j;        /* bp-0x18 */
    int16 vel2;     /* bp-0x16: clamped speed in engine units */
    int16 k;        /* bp-0x1C: pitch rotation delta */
    int16 l;        /* bp-0x20: turbulence amplitude */
    int16 dx;       /* bp-0x1E: horizontal ground speed */
    int16 m;        /* bp-0x24: roll rotation delta */
    int16 al;       /* bp-0x22 */
    int16 ei;       /* bp-0x2A */
    int16 am;       /* bp-0x28 */
    int16 n;        /* bp-0x26: heading error / delta */
    int16 gh;       /* bp-0x32 */
    int16 an;       /* bp-0x30: ring index */
    int16 ej;       /* bp-0x2E */
    int16 spdx;     /* bp-0x2C: speed calculator */

    if (g_initPhase == 0) {
        g_ourPitch =
            g_ourRoll =
                g_viewZ =
                    g_altitude =
                        g_startRange =
                            g_engineThrust =
                                g_thrust = 0;
        g_ourHead = (((uint8 FAR *)g_viewParamsFar)[0x38] & 1) ? 0 : (int16)0x8000;
        if (g_planeTable[g_selStoreIdx].flags & 0x200) {
            g_ourHead += 0x400;
        }
        rebuildOrientation();
        drawAirspeedTape();
        g_initPhase = 1;
    }
    g_keyCode = 0;
    if (kbhit() != 0) {
        g_keyCode = _bios_keybrd(0);
    }
    while (kbhit() != 0) {
        _bios_keybrd(0);
    }
    switch ((uint16)g_keyCode) {
    case 0x0C2D:
        g_engineThrust = clampRange(g_engineThrust - 0xA, 0, 0x64);
        drawAirspeedTape();
        break;
    case 0x0D3D:
        g_engineThrust = clampRange(((g_engineThrust < 0xA) ? 5 : 0xA) + g_engineThrust,
                                    0, 0x64);
        drawAirspeedTape();
        break;
    case 0x0D2B:
        g_engineThrust = 0x64;
        drawAirspeedTape();
        break;
    case 0x0C5F:
        g_engineThrust = 0;
        makeSound(0x10, 0);
        drawAirspeedTape();
        break;
    case 0x0B30:
        g_playerPlaneFlags ^= 8;
        if (!(g_playerPlaneFlags & 8) && g_groundAltitude != 0 && g_engineThrust == 0x64) {
            g_startRange = 0x546;
            makeSound(0x1C, 2);
        }
        break;
    case 0x2E63:
        g_halfScaleRender = (g_halfScaleRender == 0);
        applyViewScaleMode();
        break;
    case 0x1F00:
        if (g_joyCalibTimer == 0) {
            initJoystickCalibration();
            g_joyCalibTimer = 0x28;
        }
        break;
    case 0x1000:
        commitCommSnapshot(1);
        g_exitStatus = 0;
        break;
    case 0x3000:
        if (g_hudVisible != 0) {
            gfx_copyRect(*g_pageFront, 0, 0x6D, *g_pageOffscreen, 0, 0x6D, 0x140, 0x5B);
        }
        setDrawColor(0);
        fillRectBoth(0, 0, 0x13F, 0xC7);
        blitSprite(0, 0, 0x71, 0x41, 0xE, 7, 0);
        waitForKeyPress();
        if (g_hudVisible != 0) {
            gfx_copyRect(*g_pageOffscreen, 0, 0x6D, *g_pageFront, 0, 0x6D, 0x140, 0x5B);
            gfx_copyRect(*g_pageOffscreen, 0, 0x6D, *g_pageBack, 0, 0x6D, 0x140, 0x5B);
            drawAirspeedTape();
        }
        break;
    case 0x1900:
        waitForKeyPress();
        break;
    }
    if (g_joyCalibTimer != 0) {
        g_joyCalibTimer--;
    }
    if (g_engineThrust != 0 && g_thrust == 0) {
        makeSound(0xE, 2);
    }
    if (g_inputDisabled != 0) {
        joyAxes[0] = 0;
        joyAxes[1] = 0;
    } else if (commData->setupUseJoy != 0) {
        readCalibratedJoystick();
    } else {
        /* temp_si = g_joySensitivity + 1 */
        joyAxes[0] = (uint8)(((int16)((uint8)g_joyRawX - 0x80) * (g_joySensitivity + 1)) / 3) - 0x80;
        joyAxes[1] = (uint8)(((int16)((uint8)g_joyRawY - 0x80) * (g_joySensitivity + 1)) / 3) - 0x80;
    }
    g_rollInput = ((uint16)joyAxes[0] >> 4) - 8;
    if (g_rollInput < 0) {
        g_rollInput++;
    }
    g_pitchInput = ((uint16)joyAxes[1] >> 4) - 8;
    if (g_pitchInput < 0) {
        g_pitchInput++;
    }
    g_rollInput = -((abs(g_rollInput) + 2) * g_rollInput) * 2;
    g_pitchInput *= 6;
    if (g_pitchInput < 0) {
        g_pitchInput /= 2;
    }
    if (g_groundAltitude == g_viewZ && g_pitchInput < 0 && g_ourPitch <= 0) {
        g_pitchInput = 0;
    }
    if (g_rollInput != 0 || g_pitchInput != 0) {
        g_autopilotAltitude = 0;
    }
    drawStatusItem(4, (g_autopilotAltitude != 0) ? 0xF : 0);
    if (g_autopilotAltitude != 0) {
        n = clampValue(g_waypointBearing - g_ourHead, -0x1400, 0x1400) << 1;
        g_rollInput = -clampRange((n - g_ourRoll) >> 6, -0x18, 0x18);
        v0 = clampValue(((g_autopilotAltitude - g_viewZ) << 4) - g_rollPitchTrim,
                        -0x1400, 0xC00);
        g_pitchInput = clampRange((v0 - g_ourPitch) >> 7, -8, 8);
    }
    if (g_viewParamsFar[0x20] != 0) {
        l = (int16)(((int32)g_knots * (0x1F4 - g_viewZ)) >> 0xD);
    } else {
        l = 0;
    }
    if (!(g_playerPlaneFlags & 1)) {
        l += clampRange((g_knots - 0xC8) >> 4, 0, 0x20);
    }
    if (g_playerPlaneFlags & 2) {
        l >>= 1;
    }
    if (g_weaponMask & 4) {
        l += clampRange(g_gunHits * 4, 0, 0x20);
    }
    if (l > 0 && (uint16)g_groundAltitude < (uint16)g_viewZ) {
        g_rollInput += randomRange(l) - (l >> 1);
        g_pitchInput += (randomRange(l) - (l >> 1)) >> 1;
    }
    if ((g_playerPlaneFlags & 1) && g_pitchInput <= 0 &&
        (uint16)g_stallSpeed < (uint16)g_startRange &&
        abs(g_ourRoll) < 0x3000 && g_gunFiredFlag == 0) {
        v0 = ((((g_rollPitchTrim - g_ourPitch) >> 3) - g_viewZ) + 0xC8) >> 3;
        if (v0 > 0) {
            if ((uint16)g_viewParamsFar[0x20] < 2) {
                g_pitchInput = clampRange(v0, 0, 0x20);
            } else {
                makeSound(0xA, 1);
            }
        }
    }
    if (g_ejectState != 0) {
        g_rollInput = 0x40;
        g_pitchInput = (abs(g_ourRoll) > 0x4000) ? 0x10 : -8;
        g_crashCamZ += clampRange(-(++g_ejectState - 0x20),
                                  -0x60 / g_frameRateScaling, 0x80 / g_frameRateScaling);
        if (g_crashCamZ < 0) {
            g_crashCamZ = 0;
            if (!(g_missionTick & 7)) {
                commitCommSnapshot(0);
            }
        }
        if (g_viewZ == 0 && g_smokeSourceIdx == -1) {
            g_smokeSourceIdx = 0;
            g_selGridX = g_viewX_;
            g_selGridY = g_viewY_;
            g_hitMapX = g_viewX_;
            g_hitMapY = g_viewY_;
            g_hitAlt = 0;
            g_hitEffectTimer = -8;
            makeSound(2, 2);
            g_startRange = g_engineThrust = 0;
        }
        if ((g_ejectState & 0xFFFC) == 0x10 && ((int8)frameTick & 3) == 1) {
            g_smokeSourceIdx = -1;
            an = ((uint16)frameTick >> 1) & 7;
            g_particles[an].posX = g_viewX_;
            g_particles[an].posY = g_viewY_;
            g_particles[an].alt = g_viewZ;
            g_particles[an].spin = randomRange(0x20) << 0xB;
            g_smokeParticleSlot = an;
            g_hitMapX = g_viewX_;
            g_hitMapY = g_viewY_;
            g_hitAlt = g_viewZ;
            g_hitEffectTimer = -8;
            makeSound(0, 2);
            g_ourPitch = -0x4000;
            g_orientationDirty = 1;
        }
    }
    if ((g_weaponMask & 2) && g_engineThrust > -(g_gunHits * 4 - 0x64)) {
        g_engineThrust = -(g_gunHits * 4 - 0x64);
        if (g_engineThrust < 0) {
            g_engineThrust = 0;
        }
        drawAirspeedTape();
    }
    g_thrust += ((g_engineThrust - g_thrust) / 4) / g_frameRateScaling;
    if (g_engineThrust > g_thrust) {
        g_thrust++;
    }
    if (g_engineThrust < g_thrust) {
        g_thrust = g_engineThrust;
    }
    if ((uint16)frameTick % (uint16)(g_frameRateScaling << 1) == 0 &&
        g_engineThrust != 0) {
        g_fuelRemaining -= ((g_weaponMask & 0x20) ? g_gunHits : 0) +
                           (g_engineThrust * g_engineThrust) / 0x3E8 + 2;
    }
    if (g_fuelRemaining <= 0) {
        g_thrust = 0;
        g_fuelRemaining = 0;
    }
    g_gees = (uint8)g_geeTable[(abs(g_ourRoll) >> 8) & 0x7F];
    if ((uint16)g_groundAltitude < (uint16)g_viewZ) {
        g_gees += g_pitchInput;
    }
    if (g_gees > 0x80) {
        g_gees = 0x80;
        g_pitchInput = clampRange(-((uint8)g_geeTable[(abs(g_ourRoll) >> 8) & 0x7F] - 0x80),
                                  0, g_pitchInput);
    }
    strcpy(g_geeStrBuf, itoa(g_gees / 0x10, g_nameBuf, 10));
    strcat(g_geeStrBuf, ".");
    strcat(g_geeStrBuf, itoa((abs(g_gees) & 0xF) >> 1, g_nameBuf, 10));
    strcat(g_geeStrBuf, "G");
    spdx = (int16)(((int32)(g_thrust - sinMul(g_ourPitch, 0x78)) * 0x258) / 0x64);
    g_cornerSpeed = 0x78;
    spdx = ((((uint16)g_viewZ >> 7) + 0x400) * (int32)(spdx & spdx)) >> 10;
    g_cornerSpeed = ((((uint16)g_altitude >> 6) + 0x400) * (int32)(g_cornerSpeed & g_cornerSpeed)) >> 10;
    spdx = (int16)((-((g_fuelRemaining >> 9) - 0x64) * (int32)spdx) / 0x5A);
    spdx = (int16)(((int32)spdx * (0x80 - g_gees)) >> 7);
    g_cornerSpeed = (int16)(((int32)isqrt(g_gees * 4) * (int32)g_cornerSpeed) >> 3);
    g_cornerSpeed = abs(g_cornerSpeed);
    if (g_playerPlaneFlags & 2) {
        g_cornerSpeed -= g_cornerSpeed >> 2;
        spdx -= spdx >> 4;
    }
    if (!(g_playerPlaneFlags & 1)) {
        spdx -= spdx >> 3;
    }
    g_stallSpeed = g_cornerSpeed * 0x1B;
    vel2 = clampRange(spdx, 0, 0x383) * 0x1B;
    g_startRange += ((int32)vel2 - (int32)g_startRange) / 0x10 / g_frameRateScaling;
    g_liftForce = (int16)(((int32)g_stallSpeed * 0xC00) / (abs(g_startRange) + 1));
    if ((uint16)g_liftForce > 0x2000) {
        g_liftForce = 0x2000;
    }
    g_rollPitchTrim = cosMul(g_ourRoll, g_liftForce - 0x300);
    if (g_playerPlaneFlags & 8) {
        if (g_groundAltitude == g_viewZ) {
            g_startRange -= ((uint16)(-(g_viewParamsFar[0x20] * 8 - 0x20)) * 0x1B) /
                            (uint16)g_frameRateScaling;
            if (g_groundAltitude != 0 && (uint16)g_startRange < 0x1B0) {
                g_startRange = 0;
            }
        } else {
            g_startRange -= ((uint16)g_startRange >> 4) / (uint16)g_frameRateScaling;
        }
    }
    if ((uint16)g_startRange > 0xAFC8) {
        g_startRange = 0;
    }
    dx = cosMul(g_ourPitch, g_startRange);
    g_knots = (uint16)g_startRange / 0x1B;
    audio_setEnginePitch(g_knots, g_thrust);
    yawIn = (int16)(((int32)sinMul(g_ourRoll, g_gees << 4) << 7) /
                    ((int16)((uint16)g_startRange >> 8) + 0x20));
    yawIn = cosMul(g_ourPitch, yawIn);
    if (g_groundAltitude == g_viewZ) {
        yawIn = (-g_rollInput) << 6;
        g_rollInput = 0;
        if (g_knots < g_cornerSpeed) {
            g_pitchInput = 0;
        }
    }
    if (g_autoCrashDive != 0) {
        g_pitchInput = -0x400 - g_ourPitch;
        g_engineThrust = g_startRange = 0;
    }
    m = (int16)(((int32)g_rollInput << 7) / g_frameRateScaling);
    if (m != 0) {
        g_rollMatrix[0] = g_rollMatrix[4] = cosine(m);
        g_rollMatrix[1] = sine(m);
        g_rollMatrix[3] = -g_rollMatrix[1];
        applyRotationDelta(g_orientMatrix, g_rollMatrix);
    }
    k = (g_pitchInput << 7) / g_frameRateScaling;
    if (k != 0) {
        g_pitchMatrix[4] = g_pitchMatrix[8] = cosine(k);
        g_pitchMatrix[7] = sine(k);
        g_pitchMatrix[5] = -g_pitchMatrix[7];
        applyRotationDelta(g_orientMatrix, g_pitchMatrix);
    }
    yawAng = yawIn / g_frameRateScaling;
    if (yawAng != 0) {
        g_yawMatrix[0] = g_yawMatrix[8] = cosine(yawAng);
        g_yawMatrix[2] = sine(yawAng);
        g_yawMatrix[6] = -g_yawMatrix[2];
        applyRotationDelta(g_yawMatrix, g_orientMatrix);
    }
    computeAttitudeAngles();
    if ((uint16)g_stallSpeed > (uint16)g_startRange &&
        (uint16)g_groundAltitude < (uint16)g_viewZ) {
        g_ourPitch -= ((uint16)g_stallSpeed - (uint16)g_startRange) >>
                      (((uint16)g_viewParamsFar[0x20] == 2 || g_gunHits > 8) ? 1 : 2);
        g_orientationDirty = 1;
        if (g_ourPitch < 0 || (uint16)g_viewZ < 0xC8) {
            makeSound(0x14, 1);
        }
    }
    if (g_groundAltitude == g_viewZ) {
        if (g_ourRoll != 0) {
            g_ourRoll = 0;
            g_orientationDirty = 1;
        }
        if (g_ourPitch < 0 || (g_ourPitch > 0 && g_knots < g_cornerSpeed)) {
            if (g_autoCrashDive == 0) {
                g_ourPitch = 0;
            }
            g_orientationDirty = 1;
        }
    }
    g_autoCrashDive = 0;
    g_highGeeFlag = (abs(g_ourPitch) - abs(g_ourRoll) / 2 > 0x1000) ? 1 : 0;
    if (g_geeShakeToggle != 0) {
        g_highGeeFlag = 1;
    }
    if (g_orientationDirty != 0) {
        rebuildOrientation();
    }
    lastAlt = g_altitude;
    g_climbRate = fixedMulQ14((uint16)g_startRange >> 4,
                              sine(g_ourPitch - g_rollPitchTrim));
    g_altitude += g_climbRate / g_frameRateScaling;
    g_worldX += (fixedMulQ14(dx, sine(g_ourHead)) / 0x10) / g_frameRateScaling;
    g_worldY += (fixedMulQ14(dx, cosine(g_ourHead)) / 0x10) / g_frameRateScaling;
    if ((uint16)g_altitude > 0xF230 || (uint16)g_groundAltitude > (uint16)g_altitude) {
        g_altitude = g_groundAltitude;
    }
    if ((uint16)g_altitude > 0xEA60) {
        g_altitude = 0xEA60;
    }
    if ((uint16)g_altitude < 0x2000) {
        g_viewZ = g_altitude;
    } else if ((uint16)g_altitude < 0x4000) {
        g_viewZ = (((uint16)g_altitude - 0x2000) >> 1) + 0x2000;
    } else {
        g_viewZ = (((uint16)g_altitude - 0x4000) >> 2) + 0x3000;
    }
    if (g_groundAltitude == g_viewZ) {
        if (lastAlt > g_groundAltitude && g_inLandingCorridor != 0) {
            makeSound(0xC, 2);
            if (((g_planeTable[g_closestThreatIndex].flags & 0x200) ? 0x100 : 0x80) <
                    (uint16)g_viewParamsFar[0x20] * (uint16)-g_climbRate ||
                (g_viewParamsFar[0x20] != 0 &&
                 ((g_playerPlaneFlags & 1) ||
                  ((0x18 / (uint16)g_viewParamsFar[0x20]) << 8) <
                      (uint16)abs(g_ourRoll)))) {
                makeSound(0, 2);
                waitFrameSync(0x3C);
                commitCommSnapshot(5);
            }
        }
        g_climbRate = 0;
    }
    an = frameTick & 0xF;
    g_viewSnapshotRing[an].heading = g_ourHead;
    g_viewSnapshotRing[an].pitch = g_ourPitch;
    g_viewSnapshotRing[an].roll = g_ourRoll;
    g_viewSnapshotRing[an].worldX = g_worldX;
    g_viewSnapshotRing[an].worldY = g_worldY;
    g_viewSnapshotRing[an].alt = g_viewZ;
    if (g_currentWeaponType == 1) {
        if (g_airTargetLock >= 0) {
            an = clampRange((rangeApprox(g_viewX_ - g_simObjects[g_airTargetLock].posX,
                                         g_viewY_ - g_simObjects[g_airTargetLock].posY) *
                             g_frameRateScaling) >> 7,
                            0, 0xC);
        } else {
            an = g_frameRateScaling - 1;
        }
        an = (frameTick - an) & 0xF;
        n = g_ourHead - g_viewSnapshotRing[an].heading;
        v0 = g_ourPitch - g_viewSnapshotRing[an].pitch;
        g_aamSeekerX = cosMul(g_ourRoll, (-n) >> 2) + sinMul(g_ourRoll, v0 >> 2);
        g_aamSeekerY = sinMul(g_ourRoll, n >> 2) + cosMul(g_ourRoll, v0 >> 1);
    }
}

/* ==== seg000:0x304a ==== */
extern void far multiplyMatrix3x3Far(const int16 *, const int16 *, int16 *); /* sub_2144C */


void applyRotationDelta(const int16 *matA, const int16 *matB) {
    int16 p, a;

    g_rotationCounter++;
    if (!(*(char *)&g_rotationCounter & 7)) {
        g_orientationDirty = 1;
    }
    multiplyMatrix3x3Far(matA, matB, g_matrixScratch);
    memcpy(g_orientMatrix, g_matrixScratch, 18);
}

/* ==== seg000:0x3085 ==== */
extern int16 valueToAngle(int16);        /* sub_132F1 */
extern int16 complementAngle(int16);     /* sub_13374 */
extern int16 cosine(int16);              /* sub_11C1D — stack-arg table helper */
extern uint16 signedRatio16(int16, int16); /* sub_13277 */


void computeAttitudeAngles(void) {
    int16 cosPitch;

    g_ourPitch = valueToAngle(-g_orientMatrix[5]);
    cosPitch = cosine(g_ourPitch);
    if (cosPitch != 0) {
        if (abs(g_orientMatrix[2]) < 0x5a81) {
            g_ourHead = valueToAngle(abs((int16)signedRatio16(g_orientMatrix[2], cosPitch)));
        } else {
            g_ourHead = complementAngle(abs((int16)signedRatio16(g_orientMatrix[8], cosPitch)));
        }
        if (g_orientMatrix[2] <= 0 && g_orientMatrix[8] < 0) {
            (*((char *)&g_ourHead + 1)) += 0x80;
        }
        if (g_orientMatrix[2] > 0 && g_orientMatrix[8] < 0) {
            g_ourHead = 0x8000 - g_ourHead;
        }
        if (g_orientMatrix[2] < 0 && g_orientMatrix[8] > 0) {
            g_ourHead = -g_ourHead;
        }
        if (abs(g_orientMatrix[3]) < 0x5a81) {
            g_ourRoll = valueToAngle(abs((int16)signedRatio16(g_orientMatrix[3], cosPitch)));
        } else {
            g_ourRoll = complementAngle(abs((int16)signedRatio16(g_orientMatrix[4], cosPitch)));
        }
        if (g_orientMatrix[3] <= 0 && g_orientMatrix[4] < 0) {
            *((char *)&g_ourRoll + 1) += 0x80;
        }
        if (g_orientMatrix[3] > 0 && g_orientMatrix[4] < 0) {
            g_ourRoll = 0x8000 - g_ourRoll;
        }
        if (g_orientMatrix[3] < 0 && g_orientMatrix[4] > 0) {
            /* Force MSC to emit sub ax, ax; sub ax, g_ourRoll. */
            g_ourRoll = 0x10000 - g_ourRoll;
        }
    } else {
        g_ourRoll = 0;
        g_ourHead = valueToAngle(g_orientMatrix[1]);
        if (g_orientMatrix[3] <= 0 && g_orientMatrix[4] < 0) {
            (*((char *)&g_ourHead + 1)) += 0x80;
        }
        if (g_orientMatrix[3] > 0 && g_orientMatrix[4] < 0) {
            g_ourHead = 0x8000 - g_ourHead;
        }
        if (g_orientMatrix[3] < 0 && g_orientMatrix[4] > 0) {
            g_ourHead = -g_ourHead;
        }
    }
    if (g_ourPitch > 0x38e3 && g_ourPitch < 0x4001) {
        g_orientationDirty = 1;
    }
    if (g_ourPitch < (int16)0xc71d && g_ourPitch > (int16)0xbfff) {
        g_orientationDirty = 1;
    }
    if (g_rollWasNonzero != 0 && g_ourRoll == 0) {
        g_orientationDirty = 1;
    }
}

/* ==== seg000:0x3277 ==== */
uint16 signedRatio16(int16 numerator, int16 denominator) {
    char numeratorSign = 1;
    char denominatorSign = 1;
    int32 absNumerator;
    int32 absDenominator;

    if (numerator < 0) numeratorSign = -1;
    if (denominator < 0) denominatorSign = -1;
    absNumerator = (int32)(numerator < 0 ? -numerator : numerator);
    absDenominator = (int32)(denominator < 0 ? -denominator : denominator);
    return (uint16)((uint16)((((uint32)(uint16)absNumerator) << 16) / absDenominator >> 1)) * (uint16)(int16)numeratorSign * (uint16)(int16)denominatorSign;
}

/* ==== seg000:0x32f1 ==== */
#define ASIN_TABLE_SHIFT 9
#define WORD_DEGREE_STEP 256

int16 valueToAngle(int16 value) {
    int16 angle, magnitude, tableIndex, tableSpan;

    if (value == (int16)0x8000) return (int16)0xc000;
    magnitude = abs(value);
    tableIndex = (magnitude >> ASIN_TABLE_SHIFT) + 1;
    for (; tableIndex >= 0; tableIndex--) {
        if (g_angleLut[tableIndex] <= magnitude) {
            tableSpan = g_angleLut[tableIndex + 1] - g_angleLut[tableIndex];
            angle = (int16)((int32)(magnitude - g_angleLut[tableIndex]) * WORD_DEGREE_STEP / (int32)tableSpan) + tableIndex * WORD_DEGREE_STEP;
            break;
        }
    }
    if (value < 0) {
        angle = -angle;
    }
    return angle;
}

/* ==== seg000:0x3374 ==== */
int16 complementAngle(int16 value) {
    enum { WORD_DEGREES_QUARTER_TURN = 0x4000 };
    return WORD_DEGREES_QUARTER_TURN - valueToAngle(value);
}

/* ==== seg000:0x3253 ==== */
int far buildRotationMatrixFar(int16 *, int, int, int);

void rebuildOrientation() {
    buildRotationMatrixFar(g_orientMatrix, g_ourHead, g_ourPitch, g_ourRoll);
    g_orientationDirty = 0;
    g_rotationCounter = 0;
}

/* ==== seg000:0x33d9 ==== */
struct Projectile { int16 mapX, mapY, alt, speed, worldX, worldY, worldZ,
                    ttl, specIdx, weaponIdx, targetLock, targetRef; };
void far gfx_waitRetrace(void);                    /* sub_2F183 */
#define gfx_waitRetrace2 gfx_waitRetrace
void far gfx_waitRetrace(void);                   /* sub_2F188 */
void far gfx_nop23(void);                          /* sub_2F0D9 */
void drawAirspeedTape(void);
void insertOutlineEdges(int16 *p);
void redrawTacMap(int16 x, int16 y);               /* sub_187EC */
void fillPanelBox(int16 panelId, int16 color);     /* sub_190E8 */
void loadColorPalette(int16 mode);                 /* sub_10504 */
void render3DView(int16, int16, int16, int32, int32, int32,
                  int16, int16, int16, int16);     /* sub_1044A */
int16 clampRange(int16, int16, int16);             /* sub_1D1FA */
int16 rangeApprox(int16, int16);                   /* sub_1D23B */
int16 computeBearing(int16, int16);                /* sub_1D29D */
int16 sinMul(int16, int16);                        /* sub_1D3EC */
int16 cosMul(int16, int16);                        /* sub_1D404 */
void far gfx_copyRect(int16 src, int16 sx, int16 sy, int16 dst,
                      int16 dx, int16 dy, int16 w, int16 h);      /* sub_2F0FC */

void renderFrame(void) {
    int16 camDist, savedCamDist, rg, coffset, dx, dy, prevVis;

    g_camEyeX = g_viewTargetX = g_ViewX;
    g_camEyeY = g_ViewY;
    g_viewTargetY = 0x100000 - g_ViewY;
    g_camEyeZ = g_viewZ + 0x18;
    g_viewTargetAlt = g_viewZ;
    camDist = g_externalCamDist = clampRange(g_externalCamDist, 2, 8);
    switch (g_viewMode) {
    case 0x00:
    case 0x44:
        g_viewHeading = g_ourHead;
        g_viewPitch = g_ourPitch;
        g_viewRoll = g_ourRoll;
        break;
    case 0x41:
        g_viewHeading = g_ourHead + 0x8000;
        g_viewPitch = -g_ourPitch;
        g_viewRoll = -g_ourRoll;
        break;
    case 0x43:
        g_viewHeading = g_ourHead + 0x4000;
        g_viewPitch = -g_ourRoll;
        g_viewRoll = g_ourPitch;
        break;
    case 0x42:
        g_viewHeading = g_ourHead - 0x4000;
        g_viewPitch = g_ourRoll;
        g_viewRoll = -g_ourPitch;
        break;
    case 0x84:
        prevVis = (frameTick - g_frameRateScaling) & 0xF;
        g_viewHeading = g_viewSnapshotRing[prevVis].heading;
        g_viewPitch = g_viewSnapshotRing[prevVis].pitch;
        g_viewRoll = g_viewSnapshotRing[prevVis].roll;
        g_camEyeX = g_viewSnapshotRing[prevVis].worldX;
        g_camEyeY = g_viewSnapshotRing[prevVis].worldY;
        g_camEyeZ = g_viewSnapshotRing[prevVis].alt;
        break;
    case 0x85:
        g_viewHeading = g_ourHead - 0x4000;
        g_viewPitch = 0;
        g_viewRoll = 0;
        g_camEyeX = (int32)sinMul(g_ourHead + 0x4000, 0x18 << camDist) + g_ViewX;
        g_camEyeY = (int32)cosMul(g_ourHead + 0x4000, 0x18 << camDist) + g_ViewY;
        break;
    case 0x86:
        g_viewHeading = 0x8000;
        g_viewPitch = 0;
        g_viewRoll = 0;
        g_camEyeY = (int32)(0x18 << camDist) + g_ViewY;
        break;
    case 0x87:
        g_viewHeading = g_ourHead;
        g_viewPitch = 0;
        g_viewRoll = 0;
        g_camEyeX = (int32)sinMul(g_ourHead + 0x8000, 0x18 << camDist) + g_ViewX;
        g_camEyeY = (int32)cosMul(g_ourHead + 0x8000, 0x18 << camDist) + g_ViewY;
        g_camEyeZ = (4 << camDist) + g_viewZ;
        break;
    case 0x88:
    case 0x89:
    case 0x8B:
        if (g_viewMode != 0x89) {
            if (g_currentWeaponType == 1) {
                if (!(*(int8 *)&g_airTargetLock & 0x80))
                    g_viewTargetObj = g_airTargetLock + 0x20;
            } else {
                if (!(*(int8 *)&g_groundTargetLock & 0x80))
                    g_viewTargetObj = g_groundTargetLock + 0x40;
            }
        } else {
            if (g_inputDisabled == 0)
                g_viewTargetObj = g_lastMissileSlot;
        }
        savedCamDist = camDist;
        if (!(*(int8 *)&g_viewTargetObj & 0x40)) {
            if (!(*(int8 *)&g_viewTargetObj & 0x20)) {
                if (g_projectiles[g_viewTargetObj].ttl != 0) {
                    g_viewTargetX = (int32)(uint16)g_projectiles[g_viewTargetObj].mapX << 5;
                    g_viewTargetY = (int32)(uint16)g_projectiles[g_viewTargetObj].mapY << 5;
                    g_viewTargetAlt = g_projectiles[g_viewTargetObj].alt;
                } else {
                    g_projectiles[g_viewTargetObj].worldX = g_ourHead;
                    g_projectiles[g_viewTargetObj].worldY = g_ourPitch;
                }
                camDist = 3;
            } else {
                g_viewTargetX = g_simObjects[g_viewTargetObj & 0x1F].worldX;
                g_viewTargetY = g_simObjects[g_viewTargetObj & 0x1F].worldY;
                g_viewTargetAlt = g_simObjects[g_viewTargetObj & 0x1F].alt;
                camDist = 5;
            }
        } else {
            g_viewTargetX = (int32)(uint16)g_planeTable[g_viewTargetObj & 0x3F].mapX << 5;
            g_viewTargetY = (int32)(uint16)g_planeTable[g_viewTargetObj & 0x3F].mapY << 5;
            g_viewTargetAlt = 0x32;
            camDist = 7;
        }
        if (g_inputDisabled == 0)
            camDist = savedCamDist;
        dx = (int16)(g_viewTargetX >> 5) - g_viewX_;
        dy = (int16)(g_viewTargetY >> 5) - g_viewY_;
        rg = rangeApprox(dx, dy);
        g_viewHeading = computeBearing(dx, -dy);
        g_viewPitch = -computeBearing((g_viewTargetAlt - g_viewZ) >> 5, rg);
        g_viewRoll = 0;
        coffset = cosMul(g_viewPitch, 0x18 << camDist);
        if (*(int8 *)&g_viewTargetObj & 0x60) {
            if (g_viewMode == 0x88) {
                g_camEyeX = (int32)sinMul(g_viewHeading + 0x8000, coffset) + g_ViewX;
                g_camEyeY = (int32)cosMul(g_viewHeading + 0x8000, coffset) + g_ViewY;
                g_camEyeZ = (4 << camDist) + sinMul(g_viewPitch, 0x18 << camDist) + g_viewZ;
                g_viewPitch = -g_viewPitch;
            } else {
                g_camEyeX = (int32)sinMul(g_viewHeading, coffset) + g_viewTargetX;
                g_camEyeY = (int32)cosMul(g_viewHeading, coffset) - g_viewTargetY + 0x100000;
                g_camEyeZ = (4 << camDist) - sinMul(g_viewPitch, 0x18 << camDist) + g_viewTargetAlt;
                (*((char *)&g_viewHeading + 1)) += 0x80;
            }
        } else {
            g_viewHeading = g_projectiles[g_viewTargetObj].worldX;
            g_viewPitch = g_projectiles[g_viewTargetObj].worldY - 0x400;
            coffset = cosMul(g_viewPitch, 0x10 << camDist);
            g_camEyeX = g_viewTargetX - (int32)sinMul(g_viewHeading, coffset);
            g_camEyeY = 0x100000 - ((int32)cosMul(g_viewHeading, coffset) + g_viewTargetY);
            g_camEyeZ = g_viewTargetAlt - sinMul(g_viewPitch, 0x10 << camDist);
        }
        break;
    case 0x8C:
        g_viewPitch = 0xF400;
        g_viewRoll = 0;
        g_camEyeX = (int32)g_crashCamX << 5;
        g_camEyeY = ((int32)0x8000 - g_crashCamY) << 5;
        g_camEyeZ = g_crashCamZ;
        break;
    }
    if (abs(g_viewPitch) > 0x4000 || g_viewPitch == 0x8000) {
        g_viewPitch = 0x8000 - g_viewPitch;
        (*((char *)&g_viewHeading + 1)) += 0x80;
        g_viewRoll = 0x8000 - g_viewRoll;
    }
    if (g_viewMode == 0) {
        memcpy(g_camRotMatrix, g_orientMatrix, 0x12);
    } else {
        buildRotationMatrixFar(g_camRotMatrix, g_viewHeading, g_viewPitch, g_viewRoll);
    }
    g_camEyeZ = g_camEyeZ < 0x10 ? 0x10 : g_camEyeZ;
    prevVis = g_hudVisible;
    g_hudVisible = ((int8)g_viewMode & 0xC0) == 0;
    if (prevVis != g_hudVisible) {
        g_pageFront[8] = g_hudVisible ? 0x6C : 0xC7;
        g_pageBack[8] = g_hudVisible ? 0x6C : 0xC7;
        gfx_waitRetrace();
        if (g_hudVisible != 0) {
            gfx_nop23();
            gfx_copyRect(*g_pageOffscreen, 0, 0x6D, *g_pageFront, 0, 0x6D, 0x140, 0x5B);
            gfx_copyRect(*g_pageOffscreen, 0, 0x6D, *g_pageBack, 0, 0x6D, 0x140, 0x5B);
            drawAirspeedTape();
            if (g_mapMode == 0)
                redrawTacMap(g_viewX_, g_viewY_);
            if (g_curPanelMode == 0x13) {
                g_airTargetLock = g_groundTargetLock = 0xFFFF;
                fillPanelBox(2, 3);
                g_lockedTargetKilled = 0;
            }
        } else {
            gfx_copyRect(*g_pageOffscreen, 0, 0x96, *g_pageFront, 0, 0x96, 0x21, 0x32);
            gfx_copyRect(*g_pageFront, 0, 0x6D, *g_pageOffscreen, 0, 0x6D, 0x140, 0x5B);
        }
        gfx_waitRetrace2();
    }
    g_horizonGroundColor = (((uint8 FAR *)g_viewParamsFar)[0x38] & 2) ? 2 : 6;
    *(int8 *)&g_skyColorIndex = 3;
    if (g_detailLevel == 0 && commData->gfxModeNum != 0) {
        g_horizonGroundColor = 3;
        *(int8 *)&g_skyColorIndex = 0xB;
    }
    loadColorPalette(g_nightMode);
    g_posVisibleFlag = 0;
    render3DView(-g_viewHeading, g_viewPitch, g_viewRoll,
                 g_camEyeX, g_camEyeY, (int32)g_camEyeZ,
                 0, 0, 0x140, g_hudVisible ? 0x6D : 0xC8);
    g_extraScaleShift = 0;
    g_savedPosVisible = g_posVisibleFlag;
    switch (g_viewMode) {
    case 0x44:
        insertOutlineEdges(g_frontViewShape);
        break;
    case 0x41:
        insertOutlineEdges(g_rearViewShape);
        break;
    case 0x43:
        insertOutlineEdges(g_rightViewShape);
        break;
    case 0x42:
        insertOutlineEdges(g_leftViewShape);
        break;
    }
    g_viewClipBottom = (g_curPanelMode == 0x13 || g_mapMode == 1 || g_hudVisible == 0) ? 0xC8 : 0x6D;
}

/* ==== seg000:0x3bfb ==== */
void setDrawColor(int16 color);                      /* sub_18F9C */
void drawFullscreenLine(int16 x1, int16 y1, int16 x2, int16 y2); /* sub_18D71 */
void blitSprite(int16 dx, int16 dy, int16 sx, int16 sy, int16 w, int16 h, int16 t); /* sub_19912 */

void drawAirspeedTape(void) {
    int16 rung, i, savedPage;

    if (g_hudVisible == 0)
        return;
    gfx_copyRect(*g_pageOffscreen, 0, 0x96, *g_pageFront, 0, 0x96, 0x21, 0x32);
    rung = g_engineThrust / 5;
    for (i = -3; i < 3; i++) {
        register int16 sx, sy;
        setDrawColor(7);
        sx = rung + i;
        sy = 0xBA - rung;
        drawFullscreenLine(sx, sy, i + 0x15, 0xA5);
        setDrawColor(i == -3 ? 4 : 0xC);
        drawFullscreenLine(i, 0xBA, sx, sy);
    }
    rung = g_engineThrust / 5 - 0xB;
    g_tapeClipX = 0xC7;
    savedPage = g_drawPage;
    g_drawPage = 0;
    blitSprite(rung, 0xA0 - rung, 0x86, 0x24, 0x18, 0x10, 1);
    g_drawPage = savedPage;
    g_tapeClipX = 0x6D;
    gfx_copyRect(*g_pageFront, 0, 0x96, *g_pageBack, 0, 0x96, 0x21, 0x32);
}

/* ==== seg000:0x3d23 ==== */
void far gfx_setObjAttr(int16 attr);    /* sub_2F0CF */
void far beginEdgeGroup(void);          /* sub_21D2E */
void far insertEdge(void);              /* sub_21EB0 */
void far endEdgeGroup(void);            /* sub_21D18 */

void insertOutlineEdges(int16 *p) {
    while (*p != -1) {
        gfx_setObjAttr(((const uint8 *)*p++)[0x9E8]);
        beginEdgeGroup();
        p += 2;
        while (*p != -1) {
            g_edgeQuad[0] = p[-2];
            g_edgeQuad[2] = p[-1];
            g_edgeQuad[1] = *p++;
            g_edgeQuad[3] = *p++;
            insertEdge();
        }
        endEdgeGroup();
        p++;
    }
}

/* ==== seg000:0x3d8f ==== */
void FAR audio_engineDroneOff(void);
void updateEngineSound(void);          /* sub_1DF1A */

int16 _bios_keybrd(int16 cmd);

void waitForKeyPress(void) {
    int16 savedTiming;

    audio_engineDroneOff();
    savedTiming = g_frameTimingAccum;
loop:
    while (kbhit() == 0);
    if (_bios_keybrd(0) == 0x1900)
        goto loop;
    updateEngineSound();
    g_frameTimingAccum = savedTiming;
}
