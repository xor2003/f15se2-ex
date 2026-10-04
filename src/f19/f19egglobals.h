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

#define blitSpriteParams (*(struct SpriteParams *)(f19_dseg + 0x5856))
#define buf1_3dg ((uint8 *)(f19_dseg + 0x6ECC))
#define buf2_3dg ((uint8 *)(f19_dseg + 0x6C76))
#define buf3_3dg ((uint8 *)(f19_dseg + 0x6870))
extern uint16 buf3d3[];
extern uint8 buf3d3_1[];
extern uint8 buf3d3_2[];
extern uint8 buf3d3_3[];
#define buf4_3dg ((uint8 *)(f19_dseg + 0x666C))
#define buf_3dt ((uint8 *)(f19_dseg + 0x6FCC))
#define bulletTracks ((struct BulletTrack *)(f19_dseg + 0x9BA6))
#define colorLut ((uint8 *)(f19_dseg + 0x9E8))
#undef commData
#define commData ((struct CommData *)f19_commBase)
extern uint8 *f19eg_farPointer;   /* word_351C6/351C8 */
#define farPointer f19eg_farPointer
#define flagFarToNear (*(int16 *)(f19_dseg + 0x965A))
#define flt15_buf1 ((int16 *)(f19_dseg + 0x6378))
#define flt15_buf2 ((uint8 *)(f19_dseg + 0x238A))
#define frameTick (*(int16 *)(f19_dseg + 0x5546))
#define g_ViewX (*(int32 *)(f19_dseg + 0x8E48))
#define g_ViewY (*(int32 *)(f19_dseg + 0x9464))
#define g_aamLeadDist (*(int16 *)(f19_dseg + 0x6372))
#define g_aamSeekerX (*(int16 *)(f19_dseg + 0x9C9E))
#define g_aamSeekerY (*(int16 *)(f19_dseg + 0x9CA6))
#define g_acqAimY (*(int16 *)(f19_dseg + 0x6360))
#define g_acqRange (*(int16 *)(f19_dseg + 0x635E))
#define g_activeThreatCount (*(int16 *)(f19_dseg + 0x635C))
#define g_aimClipSave (*(int16 *)(f19_dseg + 0x636C))
#define g_airTargetLock (*(int16 *)(f19_dseg + 0x554A))
#define g_airTargetMark (*(int8 *)(f19_dseg + 0x9510))
#define g_aircraftModels ((uint8 *)((char *)f19_segPtr(f19eg_seg004) + 0x7530))
#define g_altitude (*(int16 *)(f19_dseg + 0x4708))
extern const int16 g_angleLut[];
#define g_autoCrashDive (*(int16 *)(f19_dseg + 0x664E))
#define g_autopilotAltitude (*(int16 *)(f19_dseg + 0x4F14))
extern int16 f19eg_g_autopilotEngaged;   /*  */
#define g_autopilotEngaged f19eg_g_autopilotEngaged
#define g_axisInput1 (*(int16 *)(f19_dseg + 0x6374))
#define g_axisInputAccum ((int16 *)(f19_dseg + 0x5C94))
#define g_bombDamageMask (*(int16 *)(f19_dseg + 0x4EF4))
#define g_bulletTrackCount (*(int16 *)(f19_dseg + 0x857C))
#define g_camExtFlag (*(int8 *)(f19_dseg + 0x94FE))
#define g_camEyeX (*(int32 *)(f19_dseg + 0x886C))
#define g_camEyeY (*(int32 *)(f19_dseg + 0x8B44))
#define g_camEyeZ (*(int16 *)(f19_dseg + 0x8B4E))
#define g_camRotMatrix ((int16 *)(f19_dseg + 0x80B6))
#define g_camSavedHead (*(int16 *)(f19_dseg + 0x9512))
#define g_camSavedRoll (*(int16 *)(f19_dseg + 0x6664))
#define g_chaffCount (*(int16 *)(f19_dseg + 0x4EFC))
#define g_classTab ((int8 *)(f19_dseg + 0x95F0))
#define g_climbRate (*(int16 *)(f19_dseg + 0x9EAE))
extern int16 g_clipMaxX;
extern int16 g_clipMaxY;
extern int16 f19eg_g_clipMinX;   /*  */
#define g_clipMinX f19eg_g_clipMinX
extern int16 f19eg_g_clipMinY;   /*  */
#define g_clipMinY f19eg_g_clipMinY
#define g_closestThreatIndex (*(int16 *)(f19_dseg + 0x9762))
#define g_colorPalettes ((char *)(f19_dseg + 0x3FC))
#define g_commEventFlag (*(int8 *)(f19_dseg + 0x9EA8))
#define g_cornerSpeed (*(int16 *)(f19_dseg + 0x9BA0))
#define g_crashCamX (*(int16 *)(f19_dseg + 0x9672))
#define g_crashCamY (*(int16 *)(f19_dseg + 0x9684))
#define g_crashCamZ (*(int16 *)(f19_dseg + 0x9688))
extern int16 g_curLod;
#define g_curPanelMode (*(int16 *)(f19_dseg + 0x975E))
extern struct TileSceneObject *f19eg_g_curTileEntry;   /*  */
#define g_curTileEntry f19eg_g_curTileEntry
#define g_currentWeaponType (*(int16 *)(f19_dseg + 0x9A54))
extern uint8 g_dacSupported;
#define g_damageTakenFlag (*(int16 *)(f19_dseg + 0x665A))
extern int g_detailLevel;
#define g_difficultyTier (*(int16 *)(f19_dseg + 0x4F18))
#define g_dirGridOffsets ((const int16 *)(f19_dseg + 0x43C))
extern int16 f19eg_g_drawColor;   /*  */
#define g_drawColor f19eg_g_drawColor
#define g_drawPage (*(int8 *)(f19_dseg + 0x9A5A))
#define g_dynTileEntries ((struct DynTileOverride *)(f19_dseg + 0x8B56))
#define g_edgeQuad ((int16 *)&g_lineX1)
#define g_ejectState (*(int16 *)(f19_dseg + 0x9468))
#define g_enemyAlertFlag (*(int16 *)(f19_dseg + 0x9692))
#define g_enemyGroundRemaining (*(int16 *)(f19_dseg + 0x9690))
#define g_enemyThreatCount (*(int16 *)(f19_dseg + 0x7FB4))
#define g_engineThrust (*(int16 *)(f19_dseg + 0x4718))
#define g_eventTimers ((int16 *)(f19_dseg + 0x4EF8))
#define g_exitStatus (*(int8 *)(f19_dseg + 0x75))
#define g_extViewPitch (*(int16 *)(f19_dseg + 0x663E))
#define g_externalCamDist (*(int16 *)(f19_dseg + 0x5556))
extern uint8 g_extraScaleShift;
#define g_fireCooldown (*(int16 *)(f19_dseg + 0x5C92))
#define g_fireRecs ((struct FireRec *)(f19_dseg + 0x5230))
#define g_flightPathMarkerY (*(int16 *)(f19_dseg + 0x9654))
#define g_floppyMotorPtr ((uint8 *)(f19_bda + 0x440))
#define g_frameRateAccum (*(int16 *)(f19_dseg + 0x555E))
#define g_frameRateScaling (*(int16 *)(f19_dseg + 0x4F22))
#define g_frameSyncPending (*(int8 *)(f19_dseg + 0x3F33))
#define g_frameSyncWait (*(int16 *)(f19_dseg + 0x5C8E))
#define g_frameTimingAccum (*(int16 *)(f19_dseg + 0x3F60))
#define g_frontViewShape ((int16 *)(f19_dseg + 0x471E))
#define g_fuelRemaining (*(int16 *)(f19_dseg + 0x4EF6))
#define g_geeShakeToggle (*(int16 *)(f19_dseg + 0x66D0))
#define g_geeStrBuf ((char *)(f19_dseg + 0x6640))
#define g_geeTable ((int8 *)(f19_dseg + 0x4624))
#define g_gees (*(int16 *)(f19_dseg + 0x664A))
#define g_gfxModeUnset (*(int16 *)(f19_dseg + 0x76))
#define g_gndTargetMark (*(int8 *)(f19_dseg + 0x9670))
#define g_groundAltitude (*(int16 *)(f19_dseg + 0x950A))
#define g_groundTargetLock (*(int16 *)(f19_dseg + 0x554C))
#define g_groundUnitCount (*(int16 *)(f19_dseg + 0x968E))
#define g_gunAmmo (*(int16 *)(f19_dseg + 0x4F12))
#define g_gunFiredFlag (*(int16 *)(f19_dseg + 0x6656))
#define g_gunHits (*(int16 *)(f19_dseg + 0x95DC))
extern uint8 g_halfScaleRender;
#define g_highGeeFlag (*(int8 *)(f19_dseg + 0x9C9A))
#define g_hitAlt (*(int16 *)(f19_dseg + 0x951A))
#define g_hitEffectTimer (*(int16 *)(f19_dseg + 0x6C72))
#define g_hitMapX (*(int16 *)(f19_dseg + 0x9508))
#define g_hitMapY (*(int16 *)(f19_dseg + 0x9514))
#define g_homeBaseIdx (*(int16 *)(f19_dseg + 0x87C8))
extern uint8 g_horizonGroundColor;
#define g_hudDrawnFlag (*(int8 *)(f19_dseg + 0x4285))
#define g_hudMessageBuf ((char *)(f19_dseg + 0x958C))
#define g_hudMsgTimer (*(int16 *)(f19_dseg + 0x587C))
extern int16 g_hudVisible;
#define g_inLandingCorridor (*(int16 *)(f19_dseg + 0x555A))
#define g_initPhase (*(int16 *)(f19_dseg + 0x9518))
#define g_inputDisabled (*(int16 *)(f19_dseg + 0x4F1C))
#define g_isCampaignMission (*(int16 *)(f19_dseg + 0x4F18))
#define g_itoaScratch ((char *)(f19_dseg + 0x9678))
#define g_joyCalibTimer (*(int16 *)(f19_dseg + 0x471A))
/* Virtual-stick cells: the app's int9/key-state layer (input.c updateStick)
 * maintains app-side g_joyRawX/g_joyRawY — F-19's flight model must read
 * those, not the dead dseg slots. */
extern uint8 g_joyRawX, g_joyRawY;
#define g_joySensitivity (*(int16 *)(f19_dseg + 0x5C90))
#define g_keyCode (*(int16 *)(f19_dseg + 0x965E))
#define g_knots (*(int16 *)(f19_dseg + 0x8578))
#define g_landTargetId ((int16 *)(f19_dseg + 0x9670))
#define g_landingTimer (*(int16 *)(f19_dseg + 0x5562))
#define g_lastMissileSlot (*(int16 *)(f19_dseg + 0x7FAE))
#define g_leftViewShape ((int16 *)(f19_dseg + 0x4818))
#define g_lgbCount (*(int16 *)(f19_dseg + 0x5B3C))
#define g_lgbTimer (*(int16 *)(f19_dseg + 0x5B3E))
#define g_liftForce (*(int16 *)(f19_dseg + 0x8B4A))
extern int16 g_lineX1;
extern int16 g_lineX2;
extern int16 g_lineY1;
extern int16 g_lineY2;
#define g_liveObjCount (*(int16 *)(f19_dseg + 0x968C))
#define g_lockCooldown (*(int16 *)(f19_dseg + 0x8B40))
#define g_lockMark (*(int16 *)(f19_dseg + 0x5B40))
#define g_lockedTargetKilled (*(int16 *)(f19_dseg + 0x6C74))
#define g_lodDistBase (*(int16 *)(f19_dseg + 0xA00))
#define g_lodDistFar (*(int16 *)(f19_dseg + 0xA06))
#define g_lodDistNear (*(int16 *)(f19_dseg + 0xA04))
#define g_lodDistScale (*(int16 *)(f19_dseg + 0xA02))
#define g_lodGridDim ((int16 *)(f19_dseg + 0x5F6))
#define g_lodObjectCount ((const int16 *)(f19_dseg + 0x5EA))
#define g_maneuverTable ((int16 (*)[8][8])(f19_dseg + 0x52A2))
#define g_mapCellFlags ((int8 *)(f19_dseg + 0x861A))
#define g_mapCenterX (*(int16 *)(f19_dseg + 0x5878))
#define g_mapCenterY (*(int16 *)(f19_dseg + 0x587A))
#define g_mapExtentX (*(int16 *)(f19_dseg + 0x817A))
#define g_mapExtentY (*(int16 *)(f19_dseg + 0x817C))
#define g_mapLodIndex (*(int16 *)(f19_dseg + 0x6352))
#define g_mapMode (*(int16 *)(f19_dseg + 0x9694))
#define g_mapOriginX (*(int16 *)(f19_dseg + 0x6348))
#define g_mapOriginY (*(int16 *)(f19_dseg + 0x6348))
extern int16 *f19eg_g_mapTerrainMode;   /* word_3468E */
#define g_mapTerrainMode f19eg_g_mapTerrainMode
#define g_mapTileLodTable ((const int16 *)(f19_dseg + 0x9D2))
#define g_mapX (*(int16 *)(f19_dseg + 0x3B9C))
extern int16 f19eg_g_mapY;   /*  */
#define g_mapY f19eg_g_mapY
#define g_mapZoomLevel (*(int16 *)(f19_dseg + 0x5874))
#define g_markerPosX (*(int16 *)(f19_dseg + 0x857A))
#define g_markerPosY (*(int16 *)(f19_dseg + 0x8618))
#define g_matrixScratch ((int16 *)(f19_dseg + 0x46EE))
#define g_missionStage (*(int16 *)(f19_dseg + 0x87B2))
#define g_missionStatus (*(int16 *)(f19_dseg + 0x4F16))
#define g_missionTick (*(int16 *)(f19_dseg + 0x6650))
#define g_missionTimeLimit (*(int16 *)(f19_dseg + 0x9656))
extern int16 g_modelEdgeCount;
#define g_modelEvenOddBit (*(int16 *)(f19_dseg + 0x6350))
#define g_modelOffsetTable ((uint16 *)(f19_dseg + 0x818))
extern char far *g_modelStreamPtr;
#define g_modelVertX ((uint16 *)f19eg_vertexX)
extern int16 g_modelVertY[];
extern int16 g_modelVertZ[];
#define g_modelVtxCount (*(int16 *)(f19_dseg + 0xAFA))
#define g_modelVtxXTab ((int16 *)f19eg_vertexX)
extern int16 g_modelWideVtxFlag;
#define g_nameBuf ((char *)(f19_dseg + 0x65E6))
extern char *f19eg_g_nameTab[0x68];   /* @0x9696 (word_38506) */
#define g_nameTab f19eg_g_nameTab
#define g_nearestThreatRange (*(int16 *)(f19_dseg + 0x665E))
extern struct TileObject *f19eg_g_nearestTileObj;   /* word_35CE6 */
#define g_nearestTileObj f19eg_g_nearestTileObj
#define g_neighborSampling (*(struct NeighborSampling *)(f19_dseg + 0x5BC))
#define g_nightMode (*(int16 *)(f19_dseg + 0x4F1A))
#define g_northSouthSign (*(int16 *)(f19_dseg + 0x8614))
extern int16 g_objColorBase;
extern int16 g_objDistance;
extern uint8 g_objHasRotation;
#define g_objLocalX (*(int16 *)(f19_dseg + 0x6340))
#define g_objLocalY (*(int16 *)(f19_dseg + 0x6340))
extern int16 g_objRelX;
extern int16 g_objRelY;
extern int16 g_objRenderMode;
extern uint8 g_objShade;
extern int16 g_objTransform[4];
#define g_objTypes ((struct ObjType *)(f19_dseg + 0x49D6))
extern uint8 g_offscreenRender;
#define g_orientMatrix ((int16 *)(f19_dseg + 0x46A6))
#define g_orientationDirty (*(int8 *)(f19_dseg + 0x4715))
#define g_ourHead (*(int16 *)(f19_dseg + 0x4700))
#define g_ourPitch (*(int16 *)(f19_dseg + 0x4702))
#define g_ourRoll (*(int16 *)(f19_dseg + 0x4704))
extern int16 *g_overlayCenterX;
extern int16 *g_overlayCenterY;
#define g_padlockAircraft (*(int16 *)(f19_dseg + 0x5554))
extern int16 *f19eg_g_pageBack;   /* word_3465E */
#define g_pageBack f19eg_g_pageBack
extern int16 *f19eg_g_pageFront;   /* word_34646 */
#define g_pageFront f19eg_g_pageFront
extern int16 *f19eg_g_pageOffscreen;   /* word_34676 */
#define g_pageOffscreen f19eg_g_pageOffscreen
#define g_panelLabelOn (*(int16 *)(f19_dseg + 0x4F20))
#define g_particles ((struct Particle *)(f19_dseg + 0x5260))
#define g_pitchInput (*(int16 *)(f19_dseg + 0x9B9E))
#define g_pitchMatrix ((int16 *)(f19_dseg + 0x46CA))
#define g_planeCount (*(int16 *)(f19_dseg + 0x951E))
#define g_planeScanCount (*(int16 *)(f19_dseg + 0x9C96))
#define g_planeTable ((struct MapTarget *)(f19_dseg + 0x80C8))
#define g_playerPlaneFlags (*(int16 *)(f19_dseg + 0x686C))
extern int16 g_posVisibleFlag;
#define g_prevKillMarker (*(int16 *)(f19_dseg + 0x6370))
#define g_prevScopeRange (*(int16 *)(f19_dseg + 0x9686))
#define g_prevThreatIndex (*(int16 *)(f19_dseg + 0x5558))
extern struct Proj3d f19eg_g_proj3d;   /*  */
#define g_proj3d f19eg_g_proj3d
#define g_projClipFlag (*(int8 *)(f19_dseg + 0x33D2))
#define g_projDepth (*(int16 *)(f19_dseg + 0x9660))
#define g_projectiles ((struct Projectile *)(f19_dseg + 0x5422))
#define g_radarScopeRange (*(int16 *)(f19_dseg + 0x5876))
#define g_rearViewShape ((int16 *)(f19_dseg + 0x4734))
#define g_render3DTiles (*(int16 *)(f19_dseg + 0x555C))
#define g_renderPageToggle (*(int8 *)(f19_dseg + 0x3FA))
#define g_replayCount (*(int16 *)(f19_dseg + 0x6354))
#define g_rightViewShape ((int16 *)(f19_dseg + 0x47CE))
#define g_rngSeed (*(int16 *)(f19_dseg + 0x63C6))
#define g_rocketCount (*(int16 *)(f19_dseg + 0x4EFA))
#define g_rollInput (*(int16 *)(f19_dseg + 0x9658))
#define g_rollMatrix ((int16 *)(f19_dseg + 0x46DC))
#define g_rollPitchTrim (*(int16 *)(f19_dseg + 0x6636))
#define g_rollWasNonzero (*(int8 *)(f19_dseg + 0x4712))
#define g_rotMatrix ((int16 *)(f19_dseg + 0x80B6))
#define g_rotationCounter (*(int16 *)(f19_dseg + 0x4710))
#define g_samRange (*(int16 *)(f19_dseg + 0x4E90))
#define g_samSpecs ((struct Weapon *)(f19_dseg + 0x4894))
#define g_samSpeed (*(int16 *)(f19_dseg + 0x4E92))
#define g_savedGfxOvl (*(int16 *)(f19_dseg + 0x9EB8))
#define g_savedPosVisible (*(int8 *)(f19_dseg + 0x6ECA))
#define g_scanDir (*(int16 *)(f19_dseg + 0x9EAA))
#define g_scopeArcColor (*(int16 *)(f19_dseg + 0x65E2))
#define g_scopeArcEnd (*(int16 *)(f19_dseg + 0x958A))
#define g_scopeArcRange (*(int16 *)(f19_dseg + 0x94AA))
#define g_scopeArcStart (*(int16 *)(f19_dseg + 0x9588))
#define g_scopeCenterX (*(int16 *)(f19_dseg + 0x6638))
#define g_scopeCenterY (*(int16 *)(f19_dseg + 0x663C))
#define g_scopeClipBottom (*(int16 *)(f19_dseg + 0x9B9C))
#define g_scopeClipLeft (*(int16 *)(f19_dseg + 0x9662))
#define g_scopeClipRight (*(int16 *)(f19_dseg + 0x9A58))
#define g_scopeClipTop (*(int16 *)(f19_dseg + 0x9664))
#define g_scopeSweepTimer (*(int16 *)(f19_dseg + 0x5550))
#define g_selGridX (*(int16 *)(f19_dseg + 0x80CA))
#define g_selGridY (*(int16 *)(f19_dseg + 0x80CC))
#define g_selSimObj (*(int16 *)(f19_dseg + 0x5554))
#define g_selStoreIdx (*(int16 *)(f19_dseg + 0x87B6))
#define g_selStoreState (*(int16 *)(f19_dseg + 0x554E))
#define g_selTileId (*(int16 *)(f19_dseg + 0x80D6))
#define g_setupSlots ((int16 *)(f19_dseg + 0x87B2))
#define g_shapeTargetCategory ((uint8 *)(f19_dseg + 0x95F0))
#define g_simObjects ((struct SimObject *)(f19_dseg + 0x8870))
#define g_skyColorIndex (*(int16 *)(f19_dseg + 0x9504))
#define g_smokeParticleSlot (*(int16 *)(f19_dseg + 0x52A0))
#define g_smokeSourceIdx (*(int16 *)(f19_dseg + 0x554E))
#define g_smokeTimer (*(int16 *)(f19_dseg + 0x5C9A))
extern int16 g_sortedObjCount;
#define g_soundPriorityFloor (*(int16 *)(f19_dseg + 0x5C98))
extern int16 g_spinAngle;
#define g_stallSpeed (*(int16 *)(f19_dseg + 0x7F6C))
#define g_startRange (*(int16 *)(f19_dseg + 0x7FB2))
#define g_statCells ((struct StatCell *)(f19_dseg + 0x56AA))
#define g_statTab ((int8 (*)[0xD])(f19_dseg + 0x512C))
#define g_storeDefCount (*(int16 *)(f19_dseg + 0x951E))
#define g_storeDefs ((struct StoreDef *)(f19_dseg + 0x80C8))
#define g_stores ((int16 (*)[2])(f19_dseg + 0x4F02))
#define g_stringPool ((int8 *)(f19_dseg + 0x9764))
#define g_strpool ((char *)(f19_dseg + 0x9764))
#define g_tapeClipX (*(int16 *)(f19_dseg + 0x5868))
#define g_targetBearing (*(int16 *)(f19_dseg + 0x636E))
#define g_targetEntityCount (*(int16 *)(f19_dseg + 0x6666))
#define g_targetInHudFlag (*(int16 *)(f19_dseg + 0x6A70))
#define g_targetLeadAngle (*(int16 *)(f19_dseg + 0x9760))
#define g_targetLock (*(int16 *)(f19_dseg + 0x6368))
#define g_targetRange (*(int16 *)(f19_dseg + 0x636A))
#define g_targetSlots ((struct TargetSlot *)(f19_dseg + 0x87B2))
extern int16 *f19eg_g_targetViewParams;   /* word_346A6 */
#define g_targetViewParams f19eg_g_targetViewParams
#define g_theaterGrids ((uint8 *)(f19_dseg + 0x862))
#define g_threatActiveTimer (*(int16 *)(f19_dseg + 0x5548))
#define g_threatDisplayTtl (*(int16 *)(f19_dseg + 0x6EC8))
#define g_threatLabelTarget (*(int16 *)(f19_dseg + 0x9502))
#define g_threatProxX (*(int16 *)(f19_dseg + 0x6668))
#define g_threatProxY (*(int16 *)(f19_dseg + 0x686E))
#define g_threatRadarFlag (*(int16 *)(f19_dseg + 0x9CA4))
#define g_threatRefHead (*(int16 *)(f19_dseg + 0x665C))
#define g_threatRefX (*(int16 *)(f19_dseg + 0x8B42))
#define g_threatRefY (*(int16 *)(f19_dseg + 0x8B4C))
#define g_threatRefZ (*(int16 *)(f19_dseg + 0x8B52))
#define g_threatScopeRange (*(int16 *)(f19_dseg + 0x5542))
#define g_threatSpec (*(int16 *)(f19_dseg + 0x635A))
#define g_threatTimerInit (*(int16 *)(f19_dseg + 0x871A))
#define g_threatToneLevel (*(int16 *)(f19_dseg + 0x5552))
#define g_thrust (*(int16 *)(f19_dseg + 0x8616))
#define g_tileEntryCount (*(int16 *)(f19_dseg + 0x666A))
#define g_tileEntryIdx (*(int16 *)(f19_dseg + 0x6346))
#define g_tileGridDim (*(int16 *)(f19_dseg + 0x6346))
#define g_tileKillTally ((int8 *)(f19_dseg + 0x9524))
#define g_tileWorldSize (*(int16 *)(f19_dseg + 0x6344))
#define g_tileZoomShift (*(int16 *)(f19_dseg + 0x968A))
#define g_timeAccelMode (*(uint16 *)(f19_dseg + 0x5560))
#define g_timerTick (*(int8 *)(f19_dseg + 0x3F62))
#define g_topLodGrid ((uint8 *)(f19_dseg + 0x7F6E))
#define g_trackedEnemyIdx (*(int16 *)(f19_dseg + 0x5544))
#define g_trkBearing (*(int16 *)(f19_dseg + 0x63BC))
#define g_trkPitch (*(int16 *)(f19_dseg + 0x63C2))
#define g_trkRange (*(int16 *)(f19_dseg + 0x63BA))
#define g_trkRoll (*(int16 *)(f19_dseg + 0x63C4))
#define g_trkScale (*(int16 *)(f19_dseg + 0x63C0))
#define g_trkSize (*(int16 *)(f19_dseg + 0x63BE))
#define g_unusedEventHist0 (*(int16 *)(f19_dseg + 0x9520))
#define g_unusedEventHist1 (*(int16 *)(f19_dseg + 0x95EC))
#define g_unusedEventHist2 (*(int16 *)(f19_dseg + 0x965C))
#define g_unusedFrameVal (*(int16 *)(f19_dseg + 0x65E0))
#define g_unusedLoadDoneFlag (*(int16 *)(f19_dseg + 0x962))
#define g_unusedSavedWord (*(int16 *)(f19_dseg + 0x9656))
extern int16 g_viewCenterX;
extern int16 g_viewCenterY;
#define g_viewCenterY2 g_viewCenterY   /* word_35454 (0x65E4) — same cell as the
                                        * shared Y centre: sub_11A7A writes it and
                                        * sub_1174C/sub_11802 read it. */
#define g_viewClipBottom (*(int16 *)(f19_dseg + 0x471C))
#define g_viewHeading (*(int16 *)(f19_dseg + 0x9BA4))
#define g_viewMode (*(int16 *)(f19_dseg + 0x94FE))
extern int16 *f19eg_g_viewParams;   /* word_2F268 — ptr to the transform/clip record */
#define g_viewParams f19eg_g_viewParams
#define g_viewParamsFar ((uint16 *)(f19_commBase + 0x120E))
#define g_viewPitch (*(int16 *)(f19_dseg + 0x9500))
extern int16 g_viewPosX;
extern int16 g_viewPosY;
extern int16 g_viewPosZ;
#define g_viewRoll (*(int16 *)(f19_dseg + 0x8B54))
extern int16 g_viewRotMatrix[9];
#define g_viewSnapshotRing ((struct ViewSnapshot *)(f19_dseg + 0x7FB6))
#define g_viewTargetAlt (*(int16 *)(f19_dseg + 0x9674))
#define g_viewTargetObj (*(int16 *)(f19_dseg + 0x9676))
#define g_viewTargetX (*(int32 *)(f19_dseg + 0x9666))
#define g_viewTargetY (*(int32 *)(f19_dseg + 0x966C))
#define g_viewX_ (*(uint16 *)(f19_dseg + 0x950C))
#define g_viewY_ (*(uint16 *)(f19_dseg + 0x951C))
#define g_viewZ (*(int16 *)(f19_dseg + 0x4706))
extern struct VpParms *f19eg_g_vpParms;   /* word_34646 */
#define g_vpParms f19eg_g_vpParms
#define g_vprojX (*(int16 *)(f19_dseg + 0x10B4))
#define g_vprojXlo (*(int16 *)(f19_dseg + 0x10B4))
#define g_vprojY (*(int16 *)(f19_dseg + 0x1298))
#define g_vprojYlo (*(int16 *)(f19_dseg + 0x1298))
#define g_vtxSignMaskHi (*((int16 *)&g_vtxSignMask + 1))
extern int32 g_vtxSignMask;
#define g_vtxSignMaskLo (*(int16 *)&g_vtxSignMask)
extern int16 f19eg_g_vtxX;   /*  */
#define g_vtxX f19eg_g_vtxX
extern int16 f19eg_g_vtxY;   /*  */
#define g_vtxY f19eg_g_vtxY
#define g_waterTargetId ((int16 *)(f19_dseg + 0x9510))
#define g_waypointBearing (*(int16 *)(f19_dseg + 0x94FC))
#define g_waypointNameBase (*(int16 *)(f19_dseg + 0x600))
#define g_weaponCells ((struct CellRect *)(f19_dseg + 0x5768))
#define g_weaponMask (*(int8 *)(f19_dseg + 0x4EF4))
#define g_world3dData ((char *)((char *)f19_segPtr(f19eg_seg004) + 0x0))
#define g_worldX (*(int32 *)(f19_dseg + 0x8E48))
#define g_worldY (*(int32 *)(f19_dseg + 0x9464))
#define g_wpPanelMode (*(int16 *)(f19_dseg + 0x8C46))
#define g_wpSelectIdx (*(int16 *)(f19_dseg + 0x4892))
#define g_wpnSlots ((struct WSlot *)(f19_dseg + 0x5236))
#define g_wpnSpriteX ((int16 *)(f19_dseg + 0x5968))
#define g_wpnSpriteY ((int16 *)(f19_dseg + 0x5970))
#define g_wreckAlt (*(int16 *)(f19_dseg + 0x95EE))
#define g_wreckFallVel (*(int16 *)(f19_dseg + 0x8B48))
#define g_wreckX (*(int16 *)(f19_dseg + 0x950E))
#define g_wreckY (*(int16 *)(f19_dseg + 0x9522))
#define g_yawMatrix ((int16 *)(f19_dseg + 0x46B8))
extern struct GaugeParams f19eg_gaugeSpriteParams;   /*  */
#define gaugeSpriteParams f19eg_gaugeSpriteParams
#define gfxBufPtr (*(int16 *)(f19_dseg + 0x9EAC))
#define hercFlag (*(uint8 *)(f19_dseg + 0x8B50))
#define joyAxes ((uint8 *)(f19_dseg + 0x45EA))
#define mapEvents ((struct MapEvent *)(f19_dseg + 0x5230))
#define matrix3dt ((uint16 (*)[32])(f19_dseg + 0x6D8))
#define matrix3dt_2 ((uint16 (*)[32])(f19_dseg + 0x9A5C))
#define missileSpecIndex (*(int16 *)(f19_dseg + 0x4F10))
#define missiles ((struct Missile *)(f19_dseg + 0x4F24))
#define missleSpec ((struct MissileSpec *)(f19_dseg + 0x4F00))
#define nearestTile (*(struct TileObject *)(f19_dseg + 0x8E4C))
extern char *f19eg_regnFile;   /* ->"regn.xxx" off_2EEE8 @dseg:0078 */
#define regnFile f19eg_regnFile
extern char *f19eg_regnName;   /* word_2EEE8 -> "regn.xxx" (strcpy dst) */
#define regnName f19eg_regnName
#define regnStr ((char *)(f19_dseg + 0x5C56))
#define regs (*(union REGS *)(f19_dseg + 0x95DE))
#define sams ((struct Sam *)(f19_dseg + 0x4C36))
#define scenarioPlh ((uint16 *)(f19_dseg + 0x7A))
#define sign3d3 (*(int16 *)(f19_dseg + 0x6376))
#define sign3dg (*(int16 *)(f19_dseg + 0x860))
#define sign3dt (*(int16 *)(f19_dseg + 0x6CC))
#define size3d3 (*(size_t *)(f19_dseg + 0x63B8))
#define size3d3_2 (*(int16 *)(f19_dseg + 0x6CA))
#define size3d3_3 (*(int16 *)(f19_dseg + 0x85E))
#define size3d3_4 (*(int16 *)(f19_dseg + 0x858))
#define size3d3_5 (*(int16 *)(f19_dseg + 0x85A))
#define size3d3_6 (*(int16 *)(f19_dseg + 0x85C))
#define size3d3_7 (*(int16 *)(f19_dseg + 0x9EBE))
#define sizes3dt ((uint16 *)(f19_dseg + 0x6CE))
#define strBuf ((char *)(f19_dseg + 0x65E6))
extern struct VtxScratch vtxScratch;
#define waypointIndex (*(int16 *)(f19_dseg + 0x4890))
#define waypoints ((int16 *)(f19_dseg + 0x4880))
#define g_nameTabBase (f19eg_g_nameTab[0])

#endif
