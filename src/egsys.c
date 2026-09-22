/*
 * egsys.c - system services for egame (game loop, DAC palette), isolated here
 * so a fork can replace just this file.
 */

#include "egtypes.h"
#include "egcode.h"
#include "egdata.h"
#include "math/interpolation.hpp"
#include "math/legacy_altitude.hpp"
#include "math/legacy_map.hpp"
#include "math/legacy_horizontal.hpp"
#include "egflight.h"
#include "inttype.h"
#include "gfx.h"
#include "shared/common.h"
#include "shared/blackbox.h"
#include "shared/blackbox_diag.h"

/* per-frame work reconstructed in their own TUs (egflight/egtacmap/egframe),
 * not surfaced in a header; declared here for the game loop below. */
void renderFrame(void);
void renderHudFrame(int unused);
void stepFlightModel(void);
void updateFrame(void);

/* ---- Fixed-timestep sim + interpolated rendering ----
 *
 * The original ran one sim step per rendered frame and an adaptive governor held
 * the sim cadence constant by *sleeping* the loop (waitFrameSync / g_frameSyncWait),
 * which also pinned the frame rate to the sim rate (~15 fps).
 *
 * Here the sim advances at a fixed wall-clock rate while rendering runs at the
 * display refresh, interpolating the camera between the previous and current sim
 * state. Game speed is unchanged: the governor's invariant was always
 * stepRate == g_frameRateScaling (giving the mission clock 1 Hz), so we step at
 * exactly g_frameRateScaling steps/s. The governor's auto-rescale and the sleep
 * are retired (egframe.c/egkeys.c); g_frameRateScaling is pinned at 15 (max
 * precision). ALT+A "ACCEL" (egkeys.c) multiplies the wall-clock step rate by
 * g_slowMotionMode (see simStepNsNow), a clean 2x time compression at full
 * precision. */

#define NS_PER_SEC 1000000000ULL
#define SIM_OBJ_MAX 20
#define PROJ_MAX 12
#define OBJ_TELEPORT_GUARD 0x2000 /* world units/axis; a real step moves << this */
#define SEEKER_TELEPORT_GUARD 0x400 /* seeker units/axis; a lock switch jumps >> a tracking step */

/* The authoritative sim state the camera is derived from each renderFrame(). We
 * snapshot prev/next and write an interpolated copy into the live globals just
 * for the render, then restore the authoritative "next" values for the next sim
 * step. Beyond the player's own coords (used by the forward/side/rear/chase
 * views), this includes the player *map* coords (g_viewX_/g_viewY_, used for the
 * player->target bearing in the director/target views) and the crash-cam eye
 * (g_crashCam*, the 0x8c view) so those tracking views interpolate coherently
 * too. Moving objects are interpolated separately (object snapshot helpers
 * below). */
typedef struct {
    f15::math::ViewCoordinate<f15::math::GameBackend, f15::math::ViewXAxis> viewX;
    f15::math::ViewCoordinate<f15::math::GameBackend, f15::math::ViewYAxis> viewY;
    f15::math::RenderHeight<f15::math::GameBackend> viewZ;
    f15::math::Angle<f15::math::GameBackend> head, pitch, roll;
    f15::math::MapPosition<f15::math::GameBackend> mapPos; /* g_viewX_ / g_viewY_ */
    int32 crashX, crashY, crashZ;
    int32 wreckX, wreckY, wreckAlt; /* downed-aircraft wreck/parachute */
    /* HUD reticle inputs derived from the player state each sim step (gun-reticle
     * vertical trim, air-to-air seeker head offset). They ride the same snapshot so
     * the reticles glide every render frame instead of snapping at the sim rate. */
    f15::math::Angle<f15::math::GameBackend> rollPitchTrim;
    int32 aamSeekerX, aamSeekerY;
} CamSnapshot;

static int32 iabs32(int32 v) {
    return v < 0 ? -v : v;
}

static void camCapture(CamSnapshot *s) {
    s->viewX = g_ViewX;
    s->viewY = g_ViewY;
    s->viewZ = flightSceneHeight();
    s->head = g_ourHead;
    s->pitch = g_ourPitch;
    s->roll = g_ourRoll;
    s->mapPos = flightMapPosition();
    s->crashX = g_crashCamX;
    s->crashY = g_crashCamY;
    s->crashZ = g_crashCamZ;
    s->wreckX = g_wreckX;
    s->wreckY = g_wreckY;
    s->wreckAlt = g_wreckAlt;
    s->rollPitchTrim = g_rollPitchTrim;
    s->aamSeekerX = g_aamSeekerX;
    s->aamSeekerY = g_aamSeekerY;
}

static void camRestore(const CamSnapshot *s) {
    g_ViewX = s->viewX;
    g_ViewY = s->viewY;
    g_viewZ = f15::math::legacy::Altitudes::renderWord(s->viewZ);
    g_ourHead = s->head;
    g_ourPitch = s->pitch;
    g_ourRoll = s->roll;
    g_viewX_ = f15::math::legacy::mapWordX(s->mapPos);
    g_viewY_ = f15::math::legacy::mapWordY(s->mapPos);
    g_crashCamX = (int16)s->crashX;
    g_crashCamY = (int16)s->crashY;
    g_crashCamZ = (int16)s->crashZ;
    g_wreckX = (int16)s->wreckX;
    g_wreckY = (int16)s->wreckY;
    g_wreckAlt = (int16)s->wreckAlt;
    g_rollPitchTrim = s->rollPitchTrim;
    g_aamSeekerX = (int16)s->aamSeekerX;
    g_aamSeekerY = (int16)s->aamSeekerY;
}

static int32 lerpLinear(int32 a, int32 b, int64 num, int64 den) {
    return a + (int32)(((int64)(b - a) * num) / den);
}

/* Shortest-arc interpolation in 16-bit angle space (heading wraps; pitch/roll
 * don't in practice, where the int16 delta degenerates to a plain difference). */
static int32 lerpAngle(int32 a, int32 b, int64 num, int64 den) {
    int16 d = (int16)(b - a);
    /* A single sim step never rotates the airframe ~90deg from stick input; a
     * delta that large is the discontinuous 0x8000 heading/roll flip
     * computeAttitudeAngles() emits as pitch crosses +/-90deg (gimbal). Tweening
     * across it would sweep the view 180deg for one render frame, so snap to the
     * new pose instead. */
    if (d >= 0x4000 || d <= -0x4000)
        return b;
    return a + (int32)(((int64)d * num) / den);
}

static int angleSnaps(int32 a, int32 b) {
    int16 d = (int16)(b - a);
    return d >= 0x4000 || d <= -0x4000;
}

/* Interpolate a heading/pitch/roll pose. The gimbal flip changes the
 * REPRESENTATION of all three components at once (head/roll jump 0x8000 while
 * pitch reflects with a small delta); snapping only the offending component
 * while the others keep tweening mixes two representations into a visibly
 * wrong pose (the "90deg flip-flop" when pulling through the vertical), so a
 * flip in any component snaps the whole triple to the new pose. */
static void lerpPose(int32 h0, int32 p0, int32 r0, int32 h1, int32 p1, int32 r1,
                     int64 num, int64 den, int32 *h, int32 *p, int32 *r) {
    if (angleSnaps(h0, h1) || angleSnaps(p0, p1) || angleSnaps(r0, r1)) {
        *h = h1;
        *p = p1;
        *r = r1;
    } else {
        *h = lerpAngle(h0, h1, num, den);
        *p = lerpAngle(p0, p1, num, den);
        *r = lerpAngle(r0, r1, num, den);
    }
}

static void camApplyInterp(const CamSnapshot *p, const CamSnapshot *n, int64 num, int64 den) {
    using Pose = f15::math::PoseInterpolation<f15::math::GameBackend>;
    const auto pose = Pose::interpolate({p->head, p->pitch, p->roll}, {n->head, n->pitch, n->roll},
                                       f15::math::FrameFraction::fromTicks(num, den));
    using Horizontal = f15::math::HorizontalMath<f15::math::GameBackend>;
    const auto fraction = f15::math::FrameFraction::fromTicks(num, den);
    const auto x = Horizontal::interpolate(p->viewX, n->viewX, fraction);
    const auto y = Horizontal::interpolate(p->viewY, n->viewY, fraction);
    const auto trim = Pose::snaps(p->roll, n->roll) ? n->rollPitchTrim
        : Pose::linearOffset(p->rollPitchTrim, n->rollPitchTrim, fraction);
    g_ViewX = x;
    g_ViewY = y;
    g_viewZ = f15::math::legacy::Altitudes::renderWord(
        f15::math::AltitudeMath<f15::math::GameBackend>::interpolate(
            p->viewZ, n->viewZ, f15::math::FrameFraction::fromTicks(num, den)));
    g_ourHead = pose.yaw;
    g_ourPitch = pose.pitch;
    g_ourRoll = pose.roll;
    const auto mapPos = f15::math::MapMath<f15::math::GameBackend>::interpolate(
        p->mapPos, n->mapPos, fraction);
    g_viewX_ = f15::math::legacy::mapWordX(mapPos);
    g_viewY_ = f15::math::legacy::mapWordY(mapPos);
    g_crashCamX = (int16)lerpLinear(p->crashX, n->crashX, num, den);
    g_crashCamY = (int16)lerpLinear(p->crashY, n->crashY, num, den);
    g_crashCamZ = (int16)lerpLinear(p->crashZ, n->crashZ, num, den);
    /* Wreck/parachute: alt falls per sim step. Interpolate only while present in
     * both frames and not jumped to a fresh kill (else leave at the live next). */
    if (p->wreckAlt > 0 && n->wreckAlt > 0 &&
        iabs32(n->wreckX - p->wreckX) < OBJ_TELEPORT_GUARD &&
        iabs32(n->wreckY - p->wreckY) < OBJ_TELEPORT_GUARD) {
        g_wreckX = (int16)lerpLinear(p->wreckX, n->wreckX, num, den);
        g_wreckY = (int16)lerpLinear(p->wreckY, n->wreckY, num, den);
        g_wreckAlt = (int16)lerpLinear(p->wreckAlt, n->wreckAlt, num, den);
    }
    /* Gun-reticle vertical trim tracks the roll pose; snap it across the gimbal
     * flip with the pose (else it would swing through centre for one frame). */
    g_rollPitchTrim = trim;
    /* Seeker head: snap on a lock switch (a large one-step jump), tween otherwise. */
    if (iabs32(n->aamSeekerX - p->aamSeekerX) >= SEEKER_TELEPORT_GUARD ||
        iabs32(n->aamSeekerY - p->aamSeekerY) >= SEEKER_TELEPORT_GUARD) {
        g_aamSeekerX = (int16)n->aamSeekerX;
        g_aamSeekerY = (int16)n->aamSeekerY;
    } else {
        g_aamSeekerX = (int16)lerpLinear(p->aamSeekerX, n->aamSeekerX, num, den);
        g_aamSeekerY = (int16)lerpLinear(p->aamSeekerY, n->aamSeekerY, num, den);
    }
}

static uint64 simStepNsNow(void) {
    int scaling = g_frameRateScaling;
    int accel = g_slowMotionMode; /* 2 = ACCEL (ALT+A): step 2x faster in wall-clock */
    if (scaling < 1) scaling = 1;
    if (accel < 1) accel = 1;
    return NS_PER_SEC / ((uint64)scaling * (uint64)accel);
}

/* ---- Object interpolation (stage 2) ----
 * The moving 3D scene objects — enemy aircraft (g_simObjects[]) and in-flight
 * missiles/SAMs (g_projectiles[]) — are drawn from live globals by
 * updateTargetLock() during renderFrame(). Like the camera, snapshot prev/next
 * each sim step, write an interpolated copy into the live fields for the render,
 * then restore the authoritative "next". Identity gating avoids tweening across
 * a slot reuse / teleport (which would streak):
 *   - sim objects: only while alive (flags bit1) in both frames and the world
 *     position moved less than a step could plausibly carry it (a real step
 *     advances a few hundred world units; a reuse/respawn jumps map-scale).
 *   - projectiles: ttl decrements by exactly 1 per step in flight and a reused
 *     slot always passes through ttl==0, so interpolate iff next.ttl==prev.ttl-1.
 * posX/posY mirror worldX/worldY (>>5) so the HUD reticle (projectWorldToHud,
 * which reads posX/posY) and the 3D model (drawWorldObject, worldX/worldY) stay
 * consistent. */
typedef struct {
    /* Fine-position rep captured from the object shadow — int32 under fixed,
     * fractional under modern so interpolation keeps sub-fine precision. */
    f15::math::HorizontalBoundary<f15::math::GameBackend>::Rep worldX, worldY;
    uint16 posX, posY;
    int16 alt, head, pitch, bank;
    uint8 alive;
} SimObjSnap;

typedef struct {
    f15::math::FineCoord<f15::math::GameBackend> fineX, fineY; /* authoritative mapX<<5-scale position */
    int32 alt;
    f15::math::Angle<f15::math::GameBackend> head, pitch;      /* missile yaw/pitch */
    int16 ttl;
} ProjSnap;

static int simObjCount(void) {
    int n = g_groundUnitCount;
    if (n < 0) n = 0;
    if (n > SIM_OBJ_MAX) n = SIM_OBJ_MAX;
    return n;
}

static void objCapture(SimObjSnap *sim, ProjSnap *proj) {
    int i, n = simObjCount();
    for (i = 0; i < n; i++) {
        sim[i].worldX = f15::math::legacy::objectFineRep(g_simObjectFineX[i]);
        sim[i].worldY = f15::math::legacy::objectFineRep(g_simObjectFineY[i]);
        sim[i].posX = g_simObjects[i].posX;
        sim[i].posY = g_simObjects[i].posY;
        sim[i].alt = g_simObjects[i].alt;
        sim[i].head = g_simObjects[i].heading.w;
        sim[i].pitch = g_simObjects[i].pitch;
        sim[i].bank = g_simObjects[i].bank.w;
        sim[i].alive = (g_simObjects[i].flags.b[0] & 2) ? 1 : 0;
    }
    for (i = 0; i < PROJ_MAX; i++) {
        proj[i].fineX = g_projectiles[i].fineX;
        proj[i].fineY = g_projectiles[i].fineY;
        proj[i].alt = g_projectiles[i].alt;
        proj[i].head = g_projectiles[i].head;
        proj[i].pitch = g_projectiles[i].pitch;
        proj[i].ttl = g_projectiles[i].ttl;
    }
}

static void objApplyInterp(const SimObjSnap *sp, const SimObjSnap *sn,
                           const ProjSnap *pp, const ProjSnap *pn,
                           int64 num, int64 den) {
    int i, n = simObjCount();
    using Horizontal = f15::math::HorizontalMath<f15::math::GameBackend>;
    using HBoundary = f15::math::HorizontalBoundary<f15::math::GameBackend>;
    const auto fraction = f15::math::FrameFraction::fromTicks(num, den);
    for (i = 0; i < n; i++) {
        int32 poseH, poseP, poseR;
        if (!sp[i].alive || !sn[i].alive)
            continue;
        if (std::abs(sn[i].worldX - sp[i].worldX) >= OBJ_TELEPORT_GUARD ||
            std::abs(sn[i].worldY - sp[i].worldY) >= OBJ_TELEPORT_GUARD)
            continue;
        f15::math::legacy::objectFineSet<f15::math::ViewXAxis>(
            g_simObjectFineX[i], g_simObjects[i].worldX,
            HBoundary::coordinate(Horizontal::interpolate(
                HBoundary::coordinate<f15::math::ViewXAxis>(sp[i].worldX),
                HBoundary::coordinate<f15::math::ViewXAxis>(sn[i].worldX), fraction)));
        f15::math::legacy::objectFineSet<f15::math::ViewYAxis>(
            g_simObjectFineY[i], g_simObjects[i].worldY,
            HBoundary::coordinate(Horizontal::interpolate(
                HBoundary::coordinate<f15::math::ViewYAxis>(sp[i].worldY),
                HBoundary::coordinate<f15::math::ViewYAxis>(sn[i].worldY), fraction)));
        g_simObjects[i].posX = (uint16)(g_simObjects[i].worldX >> 5);
        g_simObjects[i].posY = (uint16)(g_simObjects[i].worldY >> 5);
        g_simObjects[i].alt = (int16)lerpLinear(sp[i].alt, sn[i].alt, num, den);
        /* enemy AI flips its pose representation the same way as the player
         * (egthreat pitch>0x4000: head+=0x8000, bank+=0x8000, pitch reflected) */
        lerpPose(sp[i].head, sp[i].pitch, sp[i].bank, sn[i].head, sn[i].pitch, sn[i].bank,
                 num, den, &poseH, &poseP, &poseR);
        g_simObjects[i].heading.w = (int16)poseH;
        g_simObjects[i].pitch = (int16)poseP;
        g_simObjects[i].bank.w = (int16)poseR;
    }
    for (i = 0; i < PROJ_MAX; i++) {
        using Fine = f15::math::FineCoord<f15::math::GameBackend>;
        using Pose = f15::math::PoseInterpolation<f15::math::GameBackend>;
        const auto fraction = f15::math::FrameFraction::fromTicks(num, den);
        /* Default to the authoritative (next) position so a just-fired /
         * non-interpolated slot still has a valid fine value. */
        g_projInterpX[i] = f15::math::legacy::fineWord(pn[i].fineX);
        g_projInterpY[i] = f15::math::legacy::fineWord(pn[i].fineY);
        if (pp[i].ttl <= 0 || pn[i].ttl != pp[i].ttl - 1)
            continue;
        g_projectiles[i].fineX = Fine::interpolate(pp[i].fineX, pn[i].fineX, fraction);
        g_projectiles[i].fineY = Fine::interpolate(pp[i].fineY, pn[i].fineY, fraction);
        g_projectiles[i].mapX = g_projectiles[i].fineX.mapWord();
        g_projectiles[i].mapY = g_projectiles[i].fineY.mapWord();
        g_projInterpX[i] = f15::math::legacy::fineWord(g_projectiles[i].fineX);
        g_projInterpY[i] = f15::math::legacy::fineWord(g_projectiles[i].fineY);
        /* alt's low bit is the track-state flag (radar draws gray when clear),
         * not real altitude — interpolate the altitude but keep the authoritative
         * flag bit so "lost track" stays gray. */
        g_projectiles[i].alt = ((int16)lerpLinear(pp[i].alt, pn[i].alt, num, den) & ~1) | (pn[i].alt & 1);
        g_projectiles[i].head = Pose::angle(pp[i].head, pn[i].head, fraction);
        g_projectiles[i].pitch = Pose::angle(pp[i].pitch, pn[i].pitch, fraction);
    }
}

static void objRestore(const SimObjSnap *sn, const ProjSnap *pn) {
    int i, n = simObjCount();
    for (i = 0; i < n; i++) {
        f15::math::legacy::objectFineSet<f15::math::ViewXAxis>(
            g_simObjectFineX[i], g_simObjects[i].worldX, sn[i].worldX);
        f15::math::legacy::objectFineSet<f15::math::ViewYAxis>(
            g_simObjectFineY[i], g_simObjects[i].worldY, sn[i].worldY);
        g_simObjects[i].posX = sn[i].posX;
        g_simObjects[i].posY = sn[i].posY;
        g_simObjects[i].alt = sn[i].alt;
        g_simObjects[i].heading.w = sn[i].head;
        g_simObjects[i].pitch = sn[i].pitch;
        g_simObjects[i].bank.w = sn[i].bank;
    }
    for (i = 0; i < PROJ_MAX; i++) {
        g_projectiles[i].fineX = pn[i].fineX;
        g_projectiles[i].fineY = pn[i].fineY;
        g_projectiles[i].mapX = pn[i].fineX.mapWord();
        g_projectiles[i].mapY = pn[i].fineY.mapWord();
        g_projInterpX[i] = f15::math::legacy::fineWord(pn[i].fineX);
        g_projInterpY[i] = f15::math::legacy::fineWord(pn[i].fineY);
        g_projectiles[i].alt = (int16)pn[i].alt;
        g_projectiles[i].head = pn[i].head;
        g_projectiles[i].pitch = pn[i].pitch;
    }
}

/* gameMainLoop / runGameLoop. Each rendered frame composites the 3D world
 * (renderFrame), the tac map / 2D overlays (renderHudFrame) and the dynamic
 * gauges (unless a menu key overlay is up), then presents (gfx_dacAnimate, slot
 * 0x2c, vsync-paced). Between presents the sim is stepped fixed-rate and the
 * camera interpolated; under load the sim catches up and frames are dropped
 * rather than the game slowing. */
void gameMainLoop(void) {
    CamSnapshot camPrev, camNext;
    SimObjSnap simPrev[SIM_OBJ_MAX], simNext[SIM_OBJ_MAX];
    ProjSnap projPrev[PROJ_MAX], projNext[PROJ_MAX];
    uint64 simStepNs = simStepNsNow();
    uint64 accumNs = 0;
    uint64 prevNs = timerNowNs();
    int steps;

    camCapture(&camNext);
    camPrev = camNext; /* first frame renders the spawn state, no interpolation */
    objCapture(simNext, projNext);
    objCapture(simPrev, projPrev);

    do {
        uint64 nowNs;
        /* Record and replay consume the same pump-before-clock sequence. In
         * record mode the pump is paced by native 60 Hz time; replay consumes
         * the recorded per-pump tick count. */
        if (blackbox_enabled()) timerPump();
        nowNs = timerNowNs();
        accumNs += nowNs - prevNs;
        prevNs = nowNs;
        if (!blackbox_enabled())
            timerPump(); /* advance the 60 Hz tick counters + per-tick / colour-cycle hook */

        steps = 0;
        while (accumNs >= simStepNs) {
            accumNs -= simStepNs;
            camPrev = camNext;
            objCapture(simPrev, projPrev); /* prev = live = previous step's result */
            stepFlightModel();
            updateFrame();
            blackbox_diagCaptureSimStep();
            camCapture(&camNext);
            objCapture(simNext, projNext); /* next = live = this step's result */
            if (g_initPhase < 2) {
                /* don't interpolate across mission init's state jumps */
                camPrev = camNext;
                objCapture(simPrev, projPrev); /* live == next here, so prev = next */
            }
            simStepNs = simStepNsNow(); /* slow-motion changes the sim rate */
            if (g_missionEndedFlag[0] != 0) {
                accumNs = 0;
                break;
            }
            if (++steps >= 4) {
                accumNs = 0; /* far behind: drop the backlog, don't spiral */
                break;
            }
        }
        g_simStepsThisFrame = steps;                                   /* paces render-rate animations (g_spinAngle) to the sim */
        g_renderAlphaQ12 = (int)(((uint64)accumNs << 12) / simStepNs); /* for renderFrame's own interp (0x84 ring) */

        /* All views interpolate: camera, the player map coords + crash-cam eye,
         * and the moving objects. The director/target/crash views track an
         * object, but that object (and the player coords they bear against) are
         * now interpolated too, so the whole view stays coherent and smooth. */
        blackbox_diagBeginRenderFrame();
        camApplyInterp(&camPrev, &camNext, (int64)accumNs, (int64)simStepNs);
        objApplyInterp(simPrev, simNext, projPrev, projNext, (int64)accumNs, (int64)simStepNs);
        renderFrame();
        renderHudFrame(0);
        if (g_viewMode == VIEW_COCKPIT)
            drawInstrumentGaugesFar();
        gfx_dacAnimate();
        camRestore(&camNext); /* restore authoritative sim state for the next step */
        objRestore(simNext, projNext);
    } while (g_missionEndedFlag[0] == 0);
}

void runGameLoop(void) {
    gameMainLoop();
}

/* setupDac (egcode.asm _setupDac) - load the 256-colour palette: dacValues1 →
 * DAC entries 0x10-0x5F, dacValues (otherDacValues at night) → 0x60-0xFF, and
 * unless g_horizonGroundColor==2 the 16-entry ground ramp (g_dacGroundPaletteSrc)
 * is copied over g_dacGroundPalette (= dacValues+0x30) first. */
void setupDac(void) {
    int i;
    gfx_setDacRange(0x10, 0x50, dacValues1);
    if (g_horizonGroundColor != 2) {
        for (i = 0; i < 0x30; i++)
            dacValues[0x30 + i] = g_dacGroundPaletteSrc[i];
    }
    gfx_setDacRange(0x60, 0xA0, g_nightMode != 0 ? otherDacValues : dacValues);
    if (customWorldScenarioIs("SVN")) {
        enum { GROUND_RAMP_START = 112, GROUND_RAMP_COLORS = 16 };
        /* Six-bit VGA RGB: distant haze fades into subdued forest colors. */
        const uint8 dayHorizon[3] = {29, 33, 29};
        const uint8 dayNear[3] = {12, 17, 9};
        const uint8 nightHorizon[3] = {6, 8, 10};
        const uint8 nightNear[3] = {2, 4, 3};
        const uint8 *horizon = g_nightMode ? nightHorizon : dayHorizon;
        const uint8 *nearGround = g_nightMode ? nightNear : dayNear;
        uint8 groundRamp[GROUND_RAMP_COLORS * 3];
        for (i = 0; i < GROUND_RAMP_COLORS; ++i) {
            int channel;
            for (channel = 0; channel < 3; ++channel) {
                groundRamp[i * 3 + channel] =
                    (horizon[channel] * (GROUND_RAMP_COLORS - 1 - i) +
                     nearGround[channel] * i) / (GROUND_RAMP_COLORS - 1);
            }
        }
        gfx_setDacRange(GROUND_RAMP_START, GROUND_RAMP_COLORS, groundRamp);
    }
}
