#include "f19egvars.h"
/* generated: semantic global binds for the F-19 EGAME port */

#ifndef F19EGGLOBALS_H
#define F19EGGLOBALS_H

struct BulletTrack;
struct CellRect;
struct CommData;
struct DynTileOverride;
struct FireRec;
struct GaugeParams;
struct MapEvent;
struct MapTarget;
struct Missile;
struct MissileSpec;
struct NeighborSampling;
struct ObjType;
struct Particle;
struct Proj3d;
struct Projectile;
struct Sam;
struct SimObject;
struct SpriteParams;
struct StatCell;
struct StoreDef;
struct TargetSlot;
struct TileObject;
struct TileSceneObject;
struct ViewSnapshot;
struct VpParms;
struct VtxScratch;
struct WSlot;
struct Weapon;

#define blitSpriteParams (*(struct SpriteParams *)((uint8 *)f19_egSpace.m_g_weaponCells + 238))
#define buf1_3dg ((uint8 *)f19_egSpace.m_buf1_3dg)
#define buf2_3dg ((uint8 *)f19_egSpace.m_buf2_3dg)
#define buf3_3dg ((uint8 *)f19_egSpace.m_buf3_3dg)
extern uint16 buf3d3[];
extern uint8 buf3d3_1[];
extern uint8 buf3d3_2[];
extern uint8 buf3d3_3[];
#define buf4_3dg ((uint8 *)f19_egSpace.m_buf4_3dg)
#define buf_3dt ((uint8 *)f19_egSpace.m_buf_3dt)
#define bulletTracks ((struct BulletTrack *)f19_egSpace.m_bulletTracks)
#define colorLut ((uint8 *)f19_egSpace.m_colorLut)
#undef commData
#define commData ((struct CommData *)f19_commBase)
extern uint8 *f19eg_farPointer;   /* word_351C6/351C8 */
#define farPointer f19eg_farPointer
#define flt15_buf1 ((int16 *)f19_egSpace.m_flt15_buf1)
#define flt15_buf2 ((uint8 *)f19_egSpace.m_flt15_buf2)
#define frameTick (*(int16 *)((uint8 *)f19_egSpace.m_g_projectiles + 292))
#define g_ViewX (*(int32 *)((uint8 *)f19_egSpace.m_g_dynTileEntries + 724))
#define g_ViewY (*(int32 *)((uint8 *)f19_egSpace.m_g_dynTileEntries + 2288))
#define g_aamLeadDist (*(int16 *)((uint8 *)f19_egSpace.m_g_axisInputAccum + 1758))
#define g_aamSeekerX (*(int16 *)((uint8 *)f19_egSpace.m_bulletTracks + 248))
#define g_aamSeekerY (*(int16 *)((uint8 *)f19_egSpace.m_bulletTracks + 256))
#define g_acqAimY (*(int16 *)((uint8 *)f19_egSpace.m_g_axisInputAccum + 1740))
#define g_acqRange (*(int16 *)((uint8 *)f19_egSpace.m_g_axisInputAccum + 1738))
#define g_activeThreatCount (*(int16 *)((uint8 *)f19_egSpace.m_g_axisInputAccum + 1736))
#define g_airTargetLock (*(int16 *)((uint8 *)f19_egSpace.m_g_projectiles + 296))
#define g_aircraftModels ((uint8 *)((char *)f19_segPtr(f19eg_seg004) + 0x7530))
#define g_altitude (*(int16 *)((uint8 *)f19_egSpace.m_g_matrixScratch + 26))
extern const int16 g_angleLut[];
#define g_autoCrashDive (*(int16 *)((uint8 *)f19_egSpace.m_geeStrBuf + 14))
#define g_autopilotAltitude (*(int16 *)f19_dsegAt(0x4F14))
extern int16 f19eg_g_autopilotEngaged;   /*  */
#define g_autopilotEngaged f19eg_g_autopilotEngaged
#define g_axisInput1 (*(int16 *)((uint8 *)f19_egSpace.m_g_axisInputAccum + 1760))
#define g_axisInputAccum ((int16 *)f19_egSpace.m_g_axisInputAccum)
#define g_bombDamageMask (*(int16 *)((uint8 *)(int16 *)&f19_egSpace.m_g_bombDamageMask + 0))
#define g_bulletTrackCount (*(int16 *)((uint8 *)f19_egSpace.m_g_planeTable + 1204))
#define g_camEyeX (*(int32 *)((uint8 *)f19_egSpace.m_g_setupSlots + 186))
#define g_camEyeY (*(int32 *)((uint8 *)(int32 *)&f19_egSpace.m_g_camEyeY + 0))
#define g_camEyeZ (*(int16 *)((uint8 *)(int16 *)&f19_egSpace.m_g_camEyeZ + 0))
#define g_camRotMatrix ((int16 *)f19_egSpace.m_g_camRotMatrix)
#define g_climbRate (*(int16 *)((uint8 *)f19_egSpace.m_bulletTracks + 776))
extern int16 g_clipMaxX;
extern int16 g_clipMaxY;
extern int16 f19eg_g_clipMinX;   /*  */
#define g_clipMinX f19eg_g_clipMinX
extern int16 f19eg_g_clipMinY;   /*  */
#define g_clipMinY f19eg_g_clipMinY
#define g_closestThreatIndex (*(int16 *)((uint8 *)f19_egSpace.m_g_itoaScratch + 234))
#define g_colorPalettes ((char *)f19_egSpace.m_g_colorPalettes)
#define g_cornerSpeed (*(int16 *)((uint8 *)(int16 *)&f19_egSpace.m_g_cornerSpeed + 0))
#define g_crashCamX (*(int16 *)((uint8 *)f19_egSpace.m_g_landTargetId + 2))
#define g_crashCamY (*(int16 *)((uint8 *)f19_egSpace.m_g_itoaScratch + 12))
#define g_crashCamZ (*(int16 *)((uint8 *)f19_egSpace.m_g_itoaScratch + 16))
extern int16 g_curLod;
extern struct TileSceneObject *f19eg_g_curTileEntry;   /*  */
#define g_curTileEntry f19eg_g_curTileEntry
#define g_currentWeaponType (*(int16 *)((uint8 *)f19_egSpace.m_g_stringPool + 752))
extern uint8 g_dacSupported;
#define g_damageTakenFlag (*(int16 *)((uint8 *)f19_egSpace.m_geeStrBuf + 26))
extern int g_detailLevel;
#define g_difficultyTier (*(int16 *)f19_dsegAt(0x4F18))
#define g_dirGridOffsets ((const int16 *)f19_egSpace.m_g_dirGridOffsets)
extern int16 f19eg_g_drawColor;   /*  */
#define g_drawColor f19eg_g_drawColor
#define g_dynTileEntries ((struct DynTileOverride *)f19_egSpace.m_g_dynTileEntries)
#define g_edgeQuad ((int16 *)&g_lineX1)
#define g_ejectState (*(int16 *)((uint8 *)f19_egSpace.m_g_dynTileEntries + 2322))
#define g_enemyAlertFlag (*(int16 *)((uint8 *)f19_egSpace.m_g_itoaScratch + 26))
#define g_enemyGroundRemaining (*(int16 *)((uint8 *)f19_egSpace.m_g_itoaScratch + 24))
#define g_enemyThreatCount (*(int16 *)((uint8 *)f19_egSpace.m_g_topLodGrid + 70))
#define g_eventTimers ((int16 *)f19_egSpace.m_g_eventTimers)
#define g_extViewPitch (*(int16 *)((uint8 *)f19_egSpace.m_g_nameBuf + 88))
#define g_externalCamDist (*(int16 *)((uint8 *)f19_egSpace.m_g_projectiles + 308))
extern uint8 g_extraScaleShift;
#define g_fireCooldown (*(int16 *)((uint8 *)f19_egSpace.m_regnStr + 60))
#define g_flightPathMarkerY (*(int16 *)((uint8 *)f19_egSpace.m_g_classTab + 100))
#define g_floppyMotorPtr ((uint8 *)(f19_bda + 0x440))
#define g_frameRateAccum (*(int16 *)((uint8 *)f19_egSpace.m_g_projectiles + 316))
#define g_frameRateScaling (*(int16 *)((uint8 *)(int16 *)&f19_egSpace.m_g_frameRateScaling + 0))
#define g_frameSyncPending (*(int8 *)((uint8 *)f19_egSpace.m_sinTable + 1455))
#define g_frameSyncWait (*(int16 *)((uint8 *)f19_egSpace.m_regnStr + 56))
#define g_frameTimingAccum (*(int16 *)((uint8 *)f19_egSpace.m_sinTable + 1500))
#define g_fuelRemaining (*(int16 *)((uint8 *)(int16 *)&f19_egSpace.m_g_fuelRemaining + 0))
#define g_gees (*(int16 *)((uint8 *)f19_egSpace.m_geeStrBuf + 42))
#define g_groundAltitude (*(int16 *)((uint8 *)f19_egSpace.m_g_dynTileEntries + 2484))
#define g_groundTargetLock (*(int16 *)((uint8 *)f19_egSpace.m_g_projectiles + 298))
#define g_groundUnitCount (*(int16 *)((uint8 *)f19_egSpace.m_g_itoaScratch + 22))
#define g_gunAmmo (*(int16 *)f19_dsegAt(0x4F12))
#define g_gunFiredFlag (*(int16 *)((uint8 *)f19_egSpace.m_geeStrBuf + 22))
#define g_gunHits (*(int16 *)((uint8 *)f19_egSpace.m_g_hudMessageBuf + 80))
extern uint8 g_halfScaleRender;
#define g_highGeeFlag (*(int8 *)((uint8 *)f19_egSpace.m_bulletTracks + 244))
#define g_hitAlt (*(int16 *)((uint8 *)f19_egSpace.m_g_waterTargetId + 10))
#define g_hitEffectTimer (*(int16 *)((uint8 *)f19_egSpace.m_buf3_3dg + 1026))
#define g_hitMapX (*(int16 *)((uint8 *)f19_egSpace.m_g_dynTileEntries + 2482))
#define g_hitMapY (*(int16 *)((uint8 *)f19_egSpace.m_g_waterTargetId + 4))
extern uint8 g_horizonGroundColor;
#define g_hudDrawnFlag (*(int8 *)((uint8 *)f19_egSpace.m_sprite3 + 87))
#define g_hudMsgTimer (*(int16 *)((uint8 *)f19_egSpace.m_g_weaponCells + 276))
extern int16 g_hudVisible;
#define g_inLandingCorridor (*(int16 *)((uint8 *)f19_egSpace.m_g_projectiles + 312))
#define g_initPhase (*(int16 *)((uint8 *)f19_egSpace.m_g_waterTargetId + 8))
#define g_inputDisabled (*(int16 *)f19_dsegAt(0x4F1C))
#define g_itoaScratch ((char *)f19_egSpace.m_g_itoaScratch)
#define g_joyCalibTimer (*(int16 *)((uint8 *)f19_egSpace.m_g_matrixScratch + 44))
/* Virtual-stick cells: the app's int9/key-state layer (input.c updateStick)
 * maintains app-side g_joyRawX/g_joyRawY — F-19's flight model must read
 * those, not the dead dseg slots. */
extern uint8 g_joyRawX, g_joyRawY;
#define g_knots (*(int16 *)((uint8 *)f19_egSpace.m_g_planeTable + 1200))
#define g_landTargetId ((int16 *)f19_egSpace.m_g_landTargetId)
#define g_landingTimer (*(int16 *)((uint8 *)f19_egSpace.m_g_projectiles + 320))
#define g_lastMissileSlot (*(int16 *)((uint8 *)f19_egSpace.m_g_topLodGrid + 64))
#define g_liftForce (*(int16 *)((uint8 *)(int16 *)&f19_egSpace.m_g_liftForce + 0))
extern int16 g_lineX1;
extern int16 g_lineX2;
extern int16 g_lineY1;
extern int16 g_lineY2;
#define g_lockedTargetKilled (*(int16 *)((uint8 *)f19_egSpace.m_buf3_3dg + 1028))
#define g_lodDistBase (*(int16 *)((uint8 *)f19_egSpace.m_colorLut + 24))
#define g_lodDistFar (*(int16 *)((uint8 *)f19_egSpace.m_colorLut + 30))
#define g_lodDistNear (*(int16 *)((uint8 *)f19_egSpace.m_colorLut + 28))
#define g_lodDistScale (*(int16 *)((uint8 *)f19_egSpace.m_colorLut + 26))
#define g_lodGridDim ((int16 *)f19_egSpace.m_g_lodGridDim)
#define g_lodObjectCount ((const int16 *)f19_egSpace.m_g_lodObjectCount)
#define g_maneuverTable ((int16 (*)[8][8])((uint8 *)f19_egSpace.m_g_maneuverTable + 0))
#define g_mapCellFlags ((int8 *)f19_egSpace.m_g_mapCellFlags)
#define g_mapCenterX (*(int16 *)((uint8 *)f19_egSpace.m_g_weaponCells + 272))
#define g_mapCenterY (*(int16 *)((uint8 *)f19_egSpace.m_g_weaponCells + 274))
#define g_mapLodIndex (*(int16 *)((uint8 *)f19_egSpace.m_g_axisInputAccum + 1688))
#define g_mapMode (*(int16 *)((uint8 *)f19_egSpace.m_g_itoaScratch + 28))
#define g_mapOriginX (*(int16 *)((uint8 *)f19_egSpace.m_g_axisInputAccum + 1682))
#define g_mapOriginY (*(int16 *)((uint8 *)f19_egSpace.m_g_axisInputAccum + 1684))
extern int16 *f19eg_g_mapTerrainMode;   /* word_3468E */
#define g_mapTerrainMode f19eg_g_mapTerrainMode
#define g_mapTileLodTable ((const int16 *)f19_egSpace.m_g_mapTileLodTable)
extern int16 f19eg_g_mapY;   /*  */
#define g_mapY f19eg_g_mapY
#define g_mapZoomLevel (*(int16 *)((uint8 *)f19_egSpace.m_g_weaponCells + 268))
#define g_matrixScratch ((int16 *)f19_egSpace.m_g_matrixScratch)
#define g_missionStatus (*(int16 *)f19_dsegAt(0x4F16))
#define g_missionTick (*(int16 *)((uint8 *)f19_egSpace.m_geeStrBuf + 16))
extern int16 g_modelEdgeCount;
#define g_modelEvenOddBit (*(int16 *)((uint8 *)f19_egSpace.m_g_axisInputAccum + 1686))
#define g_modelOffsetTable ((uint16 *)f19_egSpace.m_g_modelOffsetTable)
extern char far *g_modelStreamPtr;
#define g_modelVertX ((uint16 *)f19eg_vertexX)
extern int16 g_modelVertY[];
extern int16 g_modelVertZ[];
#define g_modelVtxCount (*(int16 *)((uint8 *)f19_egSpace.m_colorLut + 274))
#define g_modelVtxXTab ((int16 *)f19eg_vertexX)
extern int16 g_modelWideVtxFlag;
extern char *f19eg_g_nameTab[0x68];   /* @0x9696 (word_38506) */
#define g_nameTab f19eg_g_nameTab
#define g_nearestThreatRange (*(int16 *)((uint8 *)f19_egSpace.m_geeStrBuf + 30))
extern struct TileObject *f19eg_g_nearestTileObj;   /* word_35CE6 */
#define g_nearestTileObj f19eg_g_nearestTileObj
#define g_neighborSampling (*(struct NeighborSampling *)((uint8 *)f19_egSpace.m_g_dirGridOffsets + 384))
#define g_nightMode (*(int16 *)f19_dsegAt(0x4F1A))
#define g_northSouthSign (*(int16 *)((uint8 *)f19_egSpace.m_g_planeTable + 1356))
extern int16 g_objColorBase;
extern int16 g_objDistance;
extern uint8 g_objHasRotation;
#define g_objLocalX (*(int16 *)((uint8 *)f19_egSpace.m_colorLut + 188))
#define g_objLocalY (*(int16 *)((uint8 *)f19_egSpace.m_colorLut + 190))
extern int16 g_objRelX;
extern int16 g_objRelY;
extern int16 g_objRenderMode;
extern uint8 g_objShade;
extern int16 g_objTransform[4];
extern uint8 g_offscreenRender;
#define g_orientMatrix ((int16 *)f19_egSpace.m_g_orientMatrix)
#define g_orientationDirty (*(int8 *)((uint8 *)f19_egSpace.m_g_matrixScratch + 39))
#define g_ourHead (*(int16 *)((uint8 *)f19_egSpace.m_g_matrixScratch + 18))
#define g_ourPitch (*(int16 *)((uint8 *)f19_egSpace.m_g_matrixScratch + 20))
#define g_ourRoll (*(int16 *)((uint8 *)f19_egSpace.m_g_matrixScratch + 22))
extern int16 *g_overlayCenterX;
extern int16 *g_overlayCenterY;
#define g_padlockAircraft (*(int16 *)((uint8 *)f19_egSpace.m_g_projectiles + 306))
extern int16 *f19eg_g_pageBack;   /* word_3465E */
#define g_pageBack f19eg_g_pageBack
extern int16 *f19eg_g_pageFront;   /* word_34646 */
#define g_pageFront f19eg_g_pageFront
extern int16 *f19eg_g_pageOffscreen;   /* word_34676 */
#define g_pageOffscreen f19eg_g_pageOffscreen
#define g_particles ((struct Particle *)f19_egSpace.m_g_particles)
#define g_pitchInput (*(int16 *)((uint8 *)(int16 *)&f19_egSpace.m_g_pitchInput + 0))
#define g_pitchMatrix ((int16 *)f19_egSpace.m_g_pitchMatrix)
#define g_planeCount (*(int16 *)((uint8 *)f19_egSpace.m_g_waterTargetId + 14))
#define g_planeScanCount (*(int16 *)((uint8 *)f19_egSpace.m_bulletTracks + 240))
#define g_planeTable ((struct MapTarget *)f19_egSpace.m_g_planeTable)
#define g_playerPlaneFlags (*(int16 *)((uint8 *)f19_egSpace.m_buf4_3dg + 512))
extern int16 g_posVisibleFlag;
#define g_prevKillMarker (*(int16 *)((uint8 *)f19_egSpace.m_g_axisInputAccum + 1756))
#define g_prevScopeRange (*(int16 *)((uint8 *)f19_egSpace.m_g_itoaScratch + 14))
#define g_prevThreatIndex (*(int16 *)((uint8 *)f19_egSpace.m_g_projectiles + 310))
extern struct Proj3d f19eg_g_proj3d;   /*  */
#define g_proj3d f19eg_g_proj3d
#define g_projDepth (*(int16 *)((uint8 *)f19_egSpace.m_g_classTab + 112))
#define g_projectiles ((struct Projectile *)f19_egSpace.m_g_projectiles)
#define g_radarScopeRange (*(int16 *)((uint8 *)f19_egSpace.m_g_weaponCells + 270))
#define g_rearViewShape ((int16 *)f19_egSpace.m_g_rearViewShape)
#define g_render3DTiles (*(int16 *)((uint8 *)f19_egSpace.m_g_projectiles + 314))
#define g_renderPageToggle (*(int8 *)f19_dsegAt(0x3FA))
#define g_rngSeed (*(int16 *)((uint8 *)f19_egSpace.m_flt15_buf1 + 78))
#define g_rollInput (*(int16 *)((uint8 *)f19_egSpace.m_g_classTab + 104))
#define g_rollMatrix ((int16 *)f19_egSpace.m_g_rollMatrix)
#define g_rollPitchTrim (*(int16 *)((uint8 *)f19_egSpace.m_g_nameBuf + 80))
#define g_rollWasNonzero (*(int8 *)((uint8 *)f19_egSpace.m_g_matrixScratch + 36))
#define g_rotationCounter (*(int16 *)((uint8 *)f19_egSpace.m_g_matrixScratch + 34))
#define g_savedPosVisible (*(int8 *)((uint8 *)f19_egSpace.m_buf2_3dg + 596))
#define g_scopeArcColor (*(int16 *)((uint8 *)f19_egSpace.m_flt15_buf1 + 618))
#define g_scopeArcEnd (*(int16 *)((uint8 *)f19_egSpace.m_g_tileKillTally + 102))
#define g_scopeArcRange (*(int16 *)((uint8 *)f19_egSpace.m_g_dynTileEntries + 2388))
#define g_scopeArcStart (*(int16 *)((uint8 *)f19_egSpace.m_g_tileKillTally + 100))
#define g_scopeCenterX (*(int16 *)((uint8 *)f19_egSpace.m_g_nameBuf + 82))
#define g_scopeCenterY (*(int16 *)((uint8 *)f19_egSpace.m_g_nameBuf + 86))
#define g_scopeClipBottom (*(int16 *)((uint8 *)(int16 *)&f19_egSpace.m_g_scopeClipBottom + 0))
#define g_scopeClipLeft (*(int16 *)((uint8 *)f19_egSpace.m_g_classTab + 114))
#define g_scopeClipRight (*(int16 *)((uint8 *)f19_egSpace.m_g_stringPool + 756))
#define g_scopeClipTop (*(int16 *)((uint8 *)f19_egSpace.m_g_classTab + 116))
#define g_scopeSweepTimer (*(int16 *)((uint8 *)f19_egSpace.m_g_projectiles + 302))
#define g_shapeTargetCategory ((uint8 *)f19_egSpace.m_g_classTab)
#define g_simObjects ((struct SimObject *)f19_egSpace.m_g_simObjects)
#define g_skyColorIndex (*(int16 *)((uint8 *)f19_egSpace.m_g_dynTileEntries + 2478))
#define g_smokeParticleSlot (*(int16 *)((uint8 *)(int16 *)&f19_egSpace.m_g_smokeParticleSlot + 0))
#define g_smokeSourceIdx (*(int16 *)((uint8 *)f19_egSpace.m_g_projectiles + 300))
extern int16 g_sortedObjCount;
extern int16 g_spinAngle;
#define g_stallSpeed (*(int16 *)((uint8 *)f19_egSpace.m_buf_3dt + 4000))
#define g_stringPool ((int8 *)f19_egSpace.m_g_stringPool)
#define g_targetBearing (*(int16 *)((uint8 *)f19_egSpace.m_g_axisInputAccum + 1754))
#define g_targetEntityCount (*(int16 *)((uint8 *)f19_egSpace.m_geeStrBuf + 38))
#define g_targetInHudFlag (*(int16 *)((uint8 *)f19_egSpace.m_buf3_3dg + 512))
#define g_targetLeadAngle (*(int16 *)((uint8 *)f19_egSpace.m_g_itoaScratch + 232))
#define g_targetRange (*(int16 *)((uint8 *)f19_egSpace.m_g_axisInputAccum + 1750))
#define g_targetSlots ((struct TargetSlot *)f19_egSpace.m_g_setupSlots)
extern int16 *f19eg_g_targetViewParams;   /* word_346A6 */
#define g_targetViewParams f19eg_g_targetViewParams
#define g_theaterGrids ((uint8 *)f19_egSpace.m_g_theaterGrids)
#define g_threatActiveTimer (*(int16 *)((uint8 *)f19_egSpace.m_g_projectiles + 294))
#define g_threatDisplayTtl (*(int16 *)((uint8 *)f19_egSpace.m_buf2_3dg + 594))
#define g_threatLabelTarget (*(int16 *)((uint8 *)f19_egSpace.m_g_dynTileEntries + 2476))
#define g_threatRadarFlag (*(int16 *)((uint8 *)f19_egSpace.m_bulletTracks + 254))
#define g_threatRefHead (*(int16 *)((uint8 *)f19_egSpace.m_geeStrBuf + 28))
#define g_threatRefX (*(int16 *)((uint8 *)(int16 *)&f19_egSpace.m_g_threatRefX + 0))
#define g_threatRefY (*(int16 *)((uint8 *)(int16 *)&f19_egSpace.m_g_threatRefY + 0))
#define g_threatRefZ (*(int16 *)((uint8 *)(int16 *)&f19_egSpace.m_g_threatRefZ + 0))
#define g_threatScopeRange (*(int16 *)((uint8 *)f19_egSpace.m_g_projectiles + 288))
#define g_threatSpec (*(int16 *)((uint8 *)f19_egSpace.m_g_axisInputAccum + 1734))
#define g_threatTimerInit (*(int16 *)((uint8 *)f19_egSpace.m_g_mapCellFlags + 256))
#define g_threatToneLevel (*(int16 *)((uint8 *)f19_egSpace.m_g_projectiles + 304))
#define g_thrust (*(int16 *)((uint8 *)f19_egSpace.m_g_planeTable + 1358))
#define g_tileEntryCount (*(int16 *)((uint8 *)f19_egSpace.m_geeStrBuf + 10))
#define g_tileEntryIdx (*(int16 *)((uint8 *)f19_egSpace.m_g_axisInputAccum + 1676))
#define g_tileGridDim (*(int16 *)((uint8 *)f19_egSpace.m_g_axisInputAccum + 1680))
#define g_tileKillTally ((int8 *)f19_egSpace.m_g_tileKillTally)
#define g_tileWorldSize (*(int16 *)((uint8 *)f19_egSpace.m_g_axisInputAccum + 1678))
#define g_tileZoomShift (*(int16 *)((uint8 *)f19_egSpace.m_g_itoaScratch + 18))
#define g_topLodGrid ((uint8 *)f19_egSpace.m_g_topLodGrid)
#define g_trackedEnemyIdx (*(int16 *)((uint8 *)f19_egSpace.m_g_projectiles + 290))
#define g_trkBearing (*(int16 *)((uint8 *)f19_egSpace.m_flt15_buf1 + 68))
#define g_trkPitch (*(int16 *)((uint8 *)f19_egSpace.m_flt15_buf1 + 74))
#define g_trkRange (*(int16 *)((uint8 *)f19_egSpace.m_flt15_buf1 + 66))
#define g_trkRoll (*(int16 *)((uint8 *)f19_egSpace.m_flt15_buf1 + 76))
#define g_trkScale (*(int16 *)((uint8 *)f19_egSpace.m_flt15_buf1 + 72))
#define g_trkSize (*(int16 *)((uint8 *)f19_egSpace.m_flt15_buf1 + 70))
#define g_unusedEventHist0 (*(int16 *)((uint8 *)f19_egSpace.m_g_waterTargetId + 16))
#define g_unusedEventHist1 (*(int16 *)((uint8 *)f19_egSpace.m_g_hudMessageBuf + 96))
#define g_unusedEventHist2 (*(int16 *)((uint8 *)f19_egSpace.m_g_classTab + 108))
#define g_unusedFrameVal (*(int16 *)((uint8 *)f19_egSpace.m_flt15_buf1 + 616))
#define g_unusedLoadDoneFlag (*(int16 *)((uint8 *)f19_egSpace.m_g_theaterGrids + 256))
#define g_unusedSavedWord (*(int16 *)((uint8 *)f19_egSpace.m_g_classTab + 102))
extern int16 g_viewCenterX;
extern int16 g_viewCenterY;
/* word_35454 (0x65E4) — same cell as the shared Y centre: sub_11A7A writes it
 * and sub_1174C/sub_11802 read it. */
#define g_viewCenterY2 g_viewCenterY
#define g_viewHeading (*(int16 *)((uint8 *)(int16 *)&f19_egSpace.m_g_viewHeading + 0))
#define g_viewMode (*(int16 *)((uint8 *)f19_egSpace.m_g_dynTileEntries + 2442))
extern int16 *f19eg_g_viewParams;   /* word_2F268 — ptr to the transform/clip record */
#define g_viewParams f19eg_g_viewParams
#define g_viewParamsFar ((uint16 *)(f19_commBase + 0x120E))
#define g_viewPitch (*(int16 *)((uint8 *)f19_egSpace.m_g_dynTileEntries + 2474))
extern int16 g_viewPosX;
extern int16 g_viewPosY;
extern int16 g_viewPosZ;
#define g_viewRoll (*(int16 *)((uint8 *)(int16 *)&f19_egSpace.m_g_viewRoll + 0))
extern int16 g_viewRotMatrix[9];
#define g_viewSnapshotRing ((struct ViewSnapshot *)f19_egSpace.m_g_viewSnapshotRing)
#define g_viewTargetAlt (*(int16 *)((uint8 *)f19_egSpace.m_g_landTargetId + 4))
#define g_viewTargetObj (*(int16 *)((uint8 *)f19_egSpace.m_g_landTargetId + 6))
#define g_viewTargetX (*(int32 *)((uint8 *)f19_egSpace.m_g_classTab + 118))
#define g_viewTargetY (*(int32 *)((uint8 *)f19_egSpace.m_g_classTab + 124))
#define g_viewX_ (*(uint16 *)((uint8 *)f19_egSpace.m_g_dynTileEntries + 2456))
#define g_viewY_ (*(uint16 *)((uint8 *)f19_egSpace.m_g_dynTileEntries + 2472))
#define g_viewZ (*(int16 *)((uint8 *)f19_egSpace.m_g_matrixScratch + 24))
extern struct VpParms *f19eg_g_vpParms;   /* word_34646 */
#define g_vpParms f19eg_g_vpParms
#define g_vtxSignMaskHi (*((int16 *)&g_vtxSignMask + 1))
extern int32 g_vtxSignMask;
#define g_vtxSignMaskLo (*(int16 *)&g_vtxSignMask)
extern int16 f19eg_g_vtxX;   /*  */
#define g_vtxX f19eg_g_vtxX
extern int16 f19eg_g_vtxY;   /*  */
#define g_vtxY f19eg_g_vtxY
#define g_waterTargetId ((int16 *)f19_egSpace.m_g_waterTargetId)
#define g_waypointBearing (*(int16 *)((uint8 *)f19_egSpace.m_g_dynTileEntries + 2470))
#define g_world3dData ((char *)((char *)f19_segPtr(f19eg_seg004) + 0x0))
#define g_wreckAlt (*(int16 *)((uint8 *)f19_egSpace.m_g_hudMessageBuf + 98))
#define g_wreckFallVel (*(int16 *)((uint8 *)(int16 *)&f19_egSpace.m_g_wreckFallVel + 0))
#define g_wreckX (*(int16 *)((uint8 *)(int16 *)&f19_egSpace.m_g_wreckX + 0))
#define g_wreckY (*(int16 *)((uint8 *)f19_egSpace.m_g_waterTargetId + 18))
#define g_yawMatrix ((int16 *)f19_egSpace.m_g_yawMatrix)
extern struct GaugeParams f19eg_gaugeSpriteParams;   /*  */
#define gaugeSpriteParams f19eg_gaugeSpriteParams
#define gfxBufPtr (*(int16 *)((uint8 *)f19_egSpace.m_bulletTracks + 774))
#define joyAxes ((uint8 *)f19_egSpace.m_joyAxes)
#define mapEvents ((struct MapEvent *)f19_egSpace.m_g_fireRecs)
#define matrix3dt ((uint16 (*)[32])((uint8 *)f19_egSpace.m_matrix3dt + 0))
#define matrix3dt_2 ((uint16 (*)[32])((uint8 *)f19_egSpace.m_matrix3dt_2 + 0))
#define missileSpecIndex (*(int16 *)f19_dsegAt(0x4F10))
#define missiles ((struct Missile *)f19_egSpace.m_missiles)
#define missleSpec ((struct MissileSpec *)f19_egSpace.m_missleSpec)
#define nearestTile (*(struct TileObject *)((uint8 *)f19_egSpace.m_g_dynTileEntries + 758))
extern char *f19eg_regnFile;   /* ->"regn.xxx" off_2EEE8 @dseg:0078 */
#define regnFile f19eg_regnFile
extern char *f19eg_regnName;   /* word_2EEE8 -> "regn.xxx" (strcpy dst) */
#define regnName f19eg_regnName
#define regnStr ((char *)f19_egSpace.m_regnStr)
#define regs (*(union REGS *)((uint8 *)f19_egSpace.m_g_hudMessageBuf + 82))
#define sams ((struct Sam *)f19_egSpace.m_sams)
#define scenarioPlh ((uint16 *)((uint8 *)(int32 *)&f19_egSpace.m_off_2EEE8 + 2))
#define sign3d3 (*(int16 *)((uint8 *)f19_egSpace.m_g_axisInputAccum + 1762))
#define sign3dg (*(int16 *)((uint8 *)f19_egSpace.m_g_modelOffsetTable + 72))
#define sign3dt (*(int16 *)((uint8 *)f19_egSpace.m_g_lodGridDim + 214))
#define size3d3 (*(size_t *)((uint8 *)f19_egSpace.m_flt15_buf1 + 64))
#define size3d3_2 (*(int16 *)((uint8 *)f19_egSpace.m_g_lodGridDim + 212))
#define size3d3_3 (*(int16 *)((uint8 *)f19_egSpace.m_g_modelOffsetTable + 70))
#define size3d3_4 (*(int16 *)((uint8 *)f19_egSpace.m_g_modelOffsetTable + 64))
#define size3d3_5 (*(int16 *)((uint8 *)f19_egSpace.m_g_modelOffsetTable + 66))
#define size3d3_6 (*(int16 *)((uint8 *)f19_egSpace.m_g_modelOffsetTable + 68))
#define size3d3_7 (*(int16 *)((uint8 *)(int16 *)&f19_egSpace.m_size3d3_7 + 0))
#define sizes3dt ((uint16 *)f19_egSpace.m_sizes3dt)
#define strBuf ((char *)f19_egSpace.m_g_nameBuf)
extern struct VtxScratch vtxScratch;
#define waypointIndex (*(int16 *)((uint8 *)f19_egSpace.m_waypoints + 16))
#define waypoints ((int16 *)f19_egSpace.m_waypoints)
#define g_nameTabBase (f19eg_g_nameTab[0])

#endif
