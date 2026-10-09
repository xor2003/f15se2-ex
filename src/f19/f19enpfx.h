/* Auto-generated: namespace every END-port global with f19en_ so the
 * ported F-19 debrief cannot bind to (or be clobbered by) the app's F-15
 * symbols or the F-19 START/EGAME twins of the same name. Included via
 * f19en.h in every END port TU.
 *
 * NOT prefixed: shared app services (my_itoa/my_ltoa, the CBreak/timer
 * handlers, resFileXxx, loadFileSection/writeFileSection, the gfx_Xxx and
 * ovlCall_Xxx driver-slot shims, timerCounter/timerPump) — those bind to
 * the existing implementations on purpose.
 */

#ifndef F19ENPFX_H
#define F19ENPFX_H

/* ---- ported END globals (defined in src/f19/en*.c) ---- */
#define allocBuffer            f19en_allocBuffer
#define allocClearBuf          f19en_allocClearBuf
#define animateFlightPath      f19en_animateFlightPath
#define blinkWidget            f19en_blinkWidget
#define calcMissionScore       f19en_calcMissionScore
#define checkAwardCodes        f19en_checkAwardCodes
#define checkPromotion         f19en_checkPromotion
#define cleanup                f19en_cleanup
#define clearActiveInRect      f19en_clearActiveInRect
#define clearKeybuf            f19en_clearKeybuf
#define closeFileWrapper       f19en_closeFileWrapper
#define createFileWrapper      f19en_createFileWrapper
#define drawClippedLine        f19en_drawClippedLine
#define drawClippedLineEx      f19en_drawClippedLineEx
#define drawEventSprite        f19en_drawEventSprite
#define drawFarString          f19en_drawFarString
#define drawFlightLine         f19en_drawFlightLine
#define drawFlightPath         f19en_drawFlightPath
#define drawMapPixel           f19en_drawMapPixel
#define drawMapView            f19en_drawMapView
#define drawMenuItem           f19en_drawMenuItem
#define drawStringAt           f19en_drawStringAt
#define drawStringAtPos        f19en_drawStringAtPos
#define drawWrappedText        f19en_drawWrappedText
#define drawWrappedTextFar     f19en_drawWrappedTextFar
#define freeBuffer             f19en_freeBuffer
#define initGraphics           f19en_initGraphics
#define isPointInRect          f19en_isPointInRect
#define loadMapView            f19en_loadMapView
#define loadPicFromFileAt      f19en_loadPicFromFileAt
#define loadWorldData          f19en_loadWorldData
#define loadWorldStrings       f19en_loadWorldStrings
#define mapToScreenX           f19en_mapToScreenX
#define mapToScreenY           f19en_mapToScreenY
#define mystrcat               f19en_mystrcat
#define mystrcpy               f19en_mystrcpy
#define openBlitClosePic       f19en_openBlitClosePic
#define openDecodeClosePic     f19en_openDecodeClosePic
#define openDecodePicAt        f19en_openDecodePicAt
#define openFileWrapper        f19en_openFileWrapper
#define parseCmd               f19en_parseCmd
#define plotMapPoint           f19en_plotMapPoint
#define processDebriefInput    f19en_processDebriefInput
#define processMenuItems       f19en_processMenuItems
#define randomRange            f19en_randomRange
#define readFile1Wrapper       f19en_readFile1Wrapper
#define readFile2Wrapper       f19en_readFile2Wrapper
#define readFromWorldBuf       f19en_readFromWorldBuf
#define readPicStream          f19en_readPicStream
#define readStageStream        f19en_readStageStream
#define readWorldData          f19en_readWorldData
#define resetRecField9         f19en_resetRecField9
#define restoreInterrupts      f19en_restoreInterrupts
#define restoreVideoMode       f19en_restoreVideoMode
#define seedRandom             f19en_seedRandom
#define selectMenuItem         f19en_selectMenuItem
#define serviceTick            f19en_serviceTick
#define setupWorldBufPtr       f19en_setupWorldBufPtr
#define stageAppend            f19en_stageAppend
#define stringWidth            f19en_stringWidth
#define tickRecAnim            f19en_tickRecAnim
#define tickRecords            f19en_tickRecords
#define timerWait              f19en_timerWait
#define waitForKeyOrJoy        f19en_waitForKeyOrJoy
#define waitForKeyOrJoy2       f19en_waitForKeyOrJoy2
#define writeFileAtRawWrapper  f19en_writeFileAtRawWrapper
#define writeToWorldBuf        f19en_writeToWorldBuf

/* ---- ported sub_* routines ---- */
#define sub_10010              f19en_sub_10010
#define sub_102FD              f19en_sub_102FD
#define sub_12EF2              f19en_sub_12EF2
#define sub_12F27              f19en_sub_12F27
#define sub_15D1B              f19en_sub_15D1B
#define sub_16076              f19en_sub_16076
#define sub_16486              f19en_sub_16486
#define sub_17094              f19en_sub_17094
#define sub_1714C              f19en_sub_1714C
#define sub_17248              f19en_sub_17248
#define sub_17280              f19en_sub_17280
#define sub_17334              f19en_sub_17334
#define sub_175BC              f19en_sub_175BC
#define sub_1883C              f19en_sub_1883C

/* ---- skeleton natives (implemented in f19enskel.c) ---- */
#define sub_10D1A              f19en_sub_10D1A      /* driver slot patcher: nop */
#define sub_10E50              f19en_sub_10E50      /* page fill via driver ops */
#define sub_11A1C              f19en_sub_11A1C      /* fd-stream cursor append */
#define sub_12FE8              f19en_sub_12FE8      /* file-stream RLE blit */
#define sub_130BE              f19en_sub_130BE      /* file bit-reader */
#define sub_13111              f19en_gety           /* staged-stream RLE blit */
#define gety                   f19en_gety
#define sub_131E7              f19en_sub_131E7      /* staged bit-reader */
#define sub_13238              f19en_picStageRefill
#define sub_13335              f19en_sub_13335      /* EGA planar write */
#define sub_133BA              f19en_sub_133BA
#define sub_133BD              f19en_sub_133BD
#define sub_133EC              f19en_sub_133EC
#define sub_13436              f19en_sub_13436
#define sub_10F0A              f19en_sub_10F0A
#define sub_1312E              f19en_sub_1312E
#define runMapView             f19en_runMapView
#define textOp_165             f19en_textOp_165
#define textOp_477             f19en_textOp_477
#define textOp_762             f19en_textOp_762
#define textOp_A61             f19en_textOp_A61
#define picStreamRead          f19en_picStreamRead
#define picReadBlock           f19en_picReadBlock
#define picStageRefill         f19en_picStageRefill
#define picBufPos              f19en_picBufPos
#define picStagePos            f19en_picStagePos

/* ---- asm/CRT helpers -> native shims ---- */
#define memsetFar              f19en_memsetFar
#define memsetNear             f19en_memsetNear
#define movedata               f19en_movedata
#define memcpyFromFar          f19en_memcpyFromFar
#define farStrcpy              f19en_farStrcpy
#define strcpyToFar            f19en_strcpyToFar
#define mystrlen               f19en_mystrlen
#define mystrchr               f19en_mystrchr
#define memeq                  f19en_memeq
#define copyBytes              f19en_copyBytes
#define dos_alloc              f19en_dos_alloc
#define dos_free               f19en_dos_free
#define dos_printstring        f19en_dos_printstring
#define dos_read               f19en_dos_read
#define intDispatch            f19en_intDispatch
#define joyTableSetup          f19en_joyTableSetup
#define pollJoystick           f19en_pollJoystick
#define readJoyAxis            f19en_readJoyAxis
#define readBiosTickLo         f19en_readBiosTickLo
#define seedRandom16           f19en_seedRandom16
#define exit                   f19en_exit
#define rand                   f19en_rand
#define srand                  f19en_srand
#define lseek                  f19en_lseek
#define seekFileAt             f19en_seekFileAt
#define drawLineWrapper        f19en_drawLineWrapper
#define clearRect              f19en_clearRect
#define int86                  f19en_int86
#define int86x                 f19en_int86x
#define segread                f19en_segread
#define enable                 f19en_enable
#define disable                f19en_disable

/* ---- misc/joystick driver-slot names -> svc wrappers ---- */
#define misc_jump_5a_keybuf    f19en_misc_jump_5a_keybuf
#define misc_jump_5b_getkey    f19en_misc_jump_5b_getkey
#define misc_jump_5d_readJoy   f19en_misc_jump_5d_readJoy
#define misc_jump_5e_clearKeyFlags f19en_misc_jump_5e_clearKeyFlags

/* ---- pic-decode cluster (skeleton asm; native shims) ---- */
#define decodePic              f19en_decodePic
#define picBlit                f19en_picBlit
#define showPicFile            f19en_showPicFile
#define doPicDecode            f19en_doPicDecode
#define picMakeDict            f19en_picMakeDict
#define picReadDataAndMakeDict f19en_picReadDataAndMakeDict
#define dictionaryLookup       f19en_dictionaryLookup

/* ---- END data symbols colliding with f15 endata.c globals ---- */
#define spriteAir            f19en_spriteAir
#define spriteAirBlink       f19en_spriteAirBlink
#define spriteGround         f19en_spriteGround
#define spriteGroundBlink    f19en_spriteGroundBlink
#define spriteSam            f19en_spriteSam
#define spriteSamBlink       f19en_spriteSamBlink
#define spriteMapArea        f19en_spriteMapArea
#define spriteWaypoint       f19en_spriteWaypoint
#define spriteWaypointBlink  f19en_spriteWaypointBlink
#define colorTablePtr        f19en_colorTablePtr
#define worldStrings         f19en_worldStrings
#define medalNames           f19en_medalNames
#define word_19806           f19en_word_19806
#define word_2379E           f19en_word_2379E
#define word_1F664           f19en_word_1F664
#define word_1F856           f19en_word_1F856
#define awardTextItem        f19en_awardTextItem
#define word_1ED12           f19en_word_1ED12
#define word_1981E           f19en_word_1981E
#define word_19704           f19en_word_19704
#define word_207B2           f19en_word_207B2
#define word_1F426           f19en_word_1F426
#define purpleHeartSpr       f19en_purpleHeartSpr
#define medalSpriteTab       f19en_medalSpriteTab
#define rankSpriteA          f19en_rankSpriteA
#define rankSpriteB          f19en_rankSpriteB
#define rankSpriteC          f19en_rankSpriteC
#define queuedAwardName      f19en_queuedAwardName
#define ribbonNames          f19en_ribbonNames
#define newRankNames         f19en_newRankNames
#define nextRankNames        f19en_nextRankNames

#define loadFileSection      f19en_loadFileSection

#endif /* F19ENPFX_H */
