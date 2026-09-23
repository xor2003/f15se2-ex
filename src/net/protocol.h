#ifndef F15_NET_PROTOCOL_H
#define F15_NET_PROTOCOL_H
/*
 * protocol.h - F15 multiplayer wire protocol (plan §3/§4/§26/§27).
 *
 * Every message starts with the NetMsgHeader (magic + version + type + tick).
 * Payloads are encoded field-by-field via serialize.h, little-endian.
 *
 * The wire speaks in *semantic* commands (NetCmd) and stick axes - never BIOS
 * scancodes, never positions. The server is authoritative: clients send intent
 * (NetInput), the server replies with results (NetSnapshot / NetEvent).
 */
#include <stdint.h>

#define F15_NET_MAGIC 0x4631354d /* 'F15M' */
#define F15_NET_VERSION 2
#define F15_NET_TICKRATE 15 /* legacy sim rate: g_frameRateScaling */
#define F15_MAX_PLAYERS 8
#define F15_MAX_COMMANDS 4  /* discrete commands a client can queue per tick */
#define F15_NAME_LEN 22     /* PILOTNAMELEN */
#define F15_MAX_SIM_OBJECTS 20
#define F15_MAX_PROJECTILES 12
#define F15_MAX_MAP_TARGETS 74
#define F15_MAX_MAP_EVENTS 4
#define F15_WAYPOINTS 4

typedef uint32_t NetEntityId;
#define NET_ENTITY_INVALID 0xFFFFFFFFu

typedef uint32_t NetTick;

enum NetMsgType {
    /* client -> server */
    NETMSG_HELLO = 1,     /* reliable: version/role/name handshake */
    NETMSG_INPUT = 2,     /* unreliable: per-tick commands + axes */
    NETMSG_BYE = 3,       /* reliable: graceful leave */
    /* server -> client */
    NETMSG_HELLO_ACK = 16,     /* reliable: accepted; carries playerId */
    NETMSG_HELLO_NAK = 17,     /* reliable: refused; carries reason text */
    NETMSG_MISSION_SETUP = 18, /* reliable: world tables + own spawn state */
    NETMSG_SNAPSHOT = 19,      /* unreliable: full world snapshot at tick */
    NETMSG_EVENT = 20,         /* reliable: semantic gameplay event */
    NETMSG_MISSION_END = 21,   /* reliable: mission over for this client */
    NETMSG_OBS = 22,           /* unreliable: AI observation (plan §20) */
};

enum NetRole {
    NET_ROLE_HUMAN = 0,
    NET_ROLE_AI = 1,
    NET_ROLE_SPECTATOR = 2,
};

/* Semantic cockpit commands (plan §3). The server-side adapter maps these back
 * to the BIOS key words the original dispatch code understands. Ordering is
 * protocol-fixed: never renumber, only append. */
enum NetCmd {
    NC_NONE = 0,
    /* flight */
    NC_THROTTLE_DOWN,   /* '-'  */
    NC_THROTTLE_UP,     /* '='  */
    NC_AFTERBURNER,     /* 'a'  */
    NC_THROTTLE_MAX,    /* '+'  (shift-=) */
    NC_THROTTLE_CUT,    /* '_'  (shift--) */
    NC_BRAKE_TOGGLE,    /* 'b'  */
    NC_GEAR_TOGGLE,     /* 'l'  */
    /* combat */
    NC_GUN_FIRE,        /* backspace: one press of the gun key */
    NC_MISSILE_FIRE,    /* enter: missile trigger key */
    NC_WEAPON_SIDEWINDER, /* 's' */
    NC_WEAPON_AMRAAM,     /* 'm' */
    NC_WEAPON_GROUND,     /* 'g' */
    NC_TARGET_DESIGNATE,  /* 't' */
    NC_FLARE,             /* 'f' */
    NC_CHAFF,             /* 'c' */
    /* radar / nav */
    NC_RADAR_RANGE,      /* 'r' */
    NC_MAP_ZOOM_IN,      /* 'z' */
    NC_MAP_ZOOM_OUT,     /* 'x' */
    NC_AUTOPILOT_TOGGLE, /* 'p' */
    NC_WAYPOINT_NEXT,    /* 'w' */
    NC_DIRECTOR_CYCLE,   /* 'd' */
    NC_ACCEL_TOGGLE,     /* ALT+a */
    /* views */
    NC_VIEW_COCKPIT,      /* space */
    NC_VIEW_FORWARD,      /* F1 */
    NC_VIEW_LEFT,         /* F2 */
    NC_VIEW_RIGHT,        /* F3 */
    NC_VIEW_REAR,         /* F4 */
    NC_VIEW_EXT_FOLLOW,   /* F5 */
    NC_VIEW_EXT_DYNAMIC,  /* F6 */
    NC_VIEW_EXT_SIDE,     /* F7 */
    NC_VIEW_MISSILE,      /* F8 */
    NC_VIEW_EXT_TARGET,   /* F9 */
    NC_VIEW_TARGET,       /* F10 */
    /* meta */
    NC_EJECT,          /* ESC (two presses eject; legacy semantics) */
    NC_ABORT_MISSION,  /* ALT+q */
    NC_DETAIL_CYCLE,   /* ALT+d */
    NC_NIGHT_TOGGLE,   /* ALT+n */
    NC_TRAINING_TOGGLE,/* ALT+t */
    NC_SOUND_CYCLE,    /* ALT+v */
    NC_KBDSENS_CYCLE,  /* ALT+k */
    NC_JOY_CALIBRATE,  /* ALT+j */
    NC_SCREENSHOT,     /* ALT+b */
    NC_PAUSE,          /* ALT+p */
    NC__COUNT
};

/* NetInput.buttons bit flags (level-triggered controls). */
enum NetButtons {
    NB_GUN = 1,     /* fire button 0 (guns)    - held = keep firing */
    NB_MISSILE = 2, /* fire button 1 (missile) - held = fire when cooled */
};

/* --- in-memory forms (wire layout is defined by the codec functions) --- */

struct NetInput {
    uint32_t clientSeq;   /* sender packet counter */
    NetTick clientTick;   /* client's own tick when sent (diagnostic) */
    uint32_t lastSnapAck; /* newest snapshot tick received (delta baseline) */
    uint8_t joyX, joyY;   /* effective stick axes, 0x80 centred (plan §3) */
    uint8_t buttons;      /* NB_* bits */
    uint8_t nCmds;        /* 0..F15_MAX_COMMANDS */
    uint8_t cmds[F15_MAX_COMMANDS]; /* NetCmd values */
};

struct NetHello {
    uint32_t protoVer;
    uint8_t role; /* NetRole */
    char name[F15_NAME_LEN];
};

struct NetHelloAck {
    uint8_t playerId;
    uint8_t tickRate;
    uint16_t flags; /* bit0: syncStep mode */
    NetTick serverTick;
};

/* Per-player block inside NetSnapshot. Mirrors the PlayerSim fields the
 * client needs to render its own cockpit/HUD authoritatively. */
struct NetPlayerState {
    int32_t worldX, worldY;   /* g_ViewX/g_ViewY (fine units) */
    int16_t alt;              /* g_viewZ */
    int16_t head, pitch, roll;
    int16_t mapX, mapY;       /* g_viewX_/g_viewY_ */
    int16_t knots;
    int16_t thrust, setThrust;
    int32_t velocity;
    uint16_t altitude;        /* g_altitude (display) */
    int16_t fuel;             /* g_fuelRemaining */
    int16_t gunAmmo;
    int16_t weaponAmmo[3];    /* missleSpec[].ammo */
    int16_t curWeapon;        /* g_currentWeaponType */
    int16_t weaponSel;        /* missileSpecIndex */
    int16_t planeFlags;       /* g_playerPlaneFlags (gear/brake bits) */
    int16_t ejectState;       /* g_ejectState */
    int16_t autopilotAlt;
    int16_t airLock, groundLock;
    int16_t radarRange;       /* g_radarScopeRange */
    int16_t waypointIdx;
    int16_t waypointBearing;
    int16_t gearArmed;        /* g_gearDownArmed */
    int16_t stallSpeed, cornerSpeed;
    int16_t aamSeekerX, aamSeekerY;
    int16_t rollPitchTrim;
    int16_t gees;
    int16_t damageFlag;       /* g_damageTakenFlag */
    uint8_t alive;            /* derived: not ended/ejected-gone */
    uint8_t missionEnded;     /* this player's mission is over */
    int16_t landingType;      /* commData->landingType equivalent */
    uint16_t score;           /* g_finalThreatScore-ish */
    /* Display state lives in the player ctx (view commands run server-side, and
     * director/autopilot code can force the view), so it must come back on the
     * wire for the owning client to render correctly. */
    uint8_t viewMode;         /* g_viewMode */
    uint8_t mapMode;          /* g_mapMode (radar/tacmap display) */
    uint8_t activePanelMode;  /* g_activePanelMode */
    uint8_t directorMode;     /* g_directorMode */
    uint8_t hudVisible;       /* g_hudVisible */
    uint8_t detailLevel;      /* g_detailLevel */
    uint8_t nightMode;        /* g_nightMode */
    uint8_t autopilotEngaged; /* g_autopilotEngaged (view camDist, HUD tag) */
    int16_t viewTargetObj;    /* g_viewTargetObj (VIEW_TARGET/MISSILE subject) */
    int16_t lastMissileSlot;  /* g_lastMissileSlot (VIEW_MISSILE subject) */
    int16_t mapZoomLevel;     /* g_mapZoomLevel */
    int16_t mapCenterX, mapCenterY;
    /* Crash-cam eye + wreck/parachute position: the camera interp snapshots
     * (egsys.c) tween these, so they must be authoritative too. */
    int16_t crashX, crashY, crashZ;
    int16_t wreckX, wreckY, wreckAlt;
};

struct NetSimObject {
    NetEntityId id;
    int32_t worldX, worldY;
    uint16_t posX, posY;
    int16_t alt;
    int16_t head, pitch, bank;
    int16_t spec;
    uint16_t flags;
    int16_t speed;
    int16_t objType;
};

struct NetProjectile {
    NetEntityId id;
    int32_t fineX, fineY;
    int16_t alt;
    int16_t ttl;
    int16_t specIdx;
};

struct NetMapTarget {
    uint16_t mapX, mapY;
    int16_t active;
    int16_t flags;
    int16_t alertLevel;
    int16_t threatTimer;
    int16_t nameIndex;
};

struct NetMapEvent {
    uint16_t mapX, mapY;
    int16_t type;
    int16_t ttl;
};

/* --- AI observation (plan §20) --- */
/* One contact the pilot is entitled to know about: the radar scope shows
 * air objects, the tacmap shows ground sites, RWR shows inbound missiles.
 * Positions arrive as bearing+range (what the instruments give the pilot);
 * ownship mapX/mapY plus these reconstruct absolute positions if needed. */
#define F15_OBS_MAX_CONTACTS 100
#define NET_ID_PROJECTILE_BASE 0x100u /* +projectile slot */
#define NET_ID_PLAYER_BASE 0x200u     /* +player index (parked remote) */
#define NET_ID_MAPTARGET_BASE 0x300u  /* +planeTable index */

enum NetObsKind {
    OBSK_AIRCRAFT = 1, /* moving g_simObjects entry (world air unit) */
    OBSK_PLAYER,       /* parked remote-player aircraft */
    OBSK_SITE,         /* g_planeTable ground/naval site (tacmap contact) */
    OBSK_MISSILE,      /* in-flight projectile */
};

enum NetObsFlags {
    OBSF_LOCKED_AIR = 1,   /* this contact == ownship airTargetLock */
    OBSF_LOCKED_GND = 2,   /* this contact == ownship groundTargetLock */
    OBSF_THREAT = 4,       /* closestThreatIndex (RWR spike source) */
    OBSF_ACTIVE = 8,       /* site is alerted/active */
    OBSF_INBOUND = 16,     /* missile is targeting this observer */
};

struct NetObsContact {
    NetEntityId id;  /* base + index, see NET_ID_*_BASE */
    int16_t relBear; /* bearing - ownship heading (same units as ourHead) */
    uint16_t range;  /* map units (rangeApprox, capped 0x7fff) */
    int16_t altDelta; /* contact alt - ownship alt */
    int16_t heading; /* absolute heading (air/missile; 0 for sites) */
    int16_t speed;   /* knots (0 for sites) */
    uint8_t kind;    /* NetObsKind */
    uint8_t flags;   /* NetObsFlags bits */
};

/* Decoded NETMSG_OBS tail (ownship block rides NetPlayerState). */
struct NetObs {
    int16_t threatId;     /* entity id of RWR spike source, -1 none */
    int16_t threatRange;  /* map units */
    int16_t threatBearing; /* relative bearing to threat */
    int16_t missionTick;
    int16_t missionStatus;
    uint8_t nContacts;
    struct NetObsContact contacts[F15_OBS_MAX_CONTACTS];
    uint32_t stateHash;   /* same canonical hash as the snapshot trailer */
};

/* Mission bootstrap tables (reliable, once). Mirrors worldImportToEgame's
 * inputs: the client replays them locally to set up the same world. */
struct NetMissionSetup {
    int16_t theater;
    int16_t difficulty;
    int32_t seed;
    int16_t planeCount;
    int16_t targetEntityCount;
    int16_t planeScanCount;
    int16_t groundUnitCount;
    /* variable-length tail encoded by codec: */
    /*   NetMapTarget planeTable[planeCount]                             */
    /*   NetSimObject flightUnits[groundUnitCount]                       */
    /*   uint8 nameIndexLead? (folded into planeTable[0] handling)       */
    /*   waypoints[F15_WAYPOINTS] (mapX,mapY u16 each)                   */
    /*   targetSlots (2 x Target fields)                                 */
    /*   stringPool[0x2EE], mapCellFlags[0x100], shapeTargetCategory[100],
     *   tileKillTally[100], landTargetId, waterTargetId                */
};

struct NetEvent {
    uint16_t eventType; /* NetEventType */
    NetEntityId subject;
    NetEntityId object;
    int16_t arg;
    char text[40]; /* optional message (hudMessage text) */
};

enum NetEventType {
    NE_HUD_MESSAGE = 1,
    NE_TIMED_MESSAGE,
    NE_SOUND,
    NE_VOICE_CUE,
    NE_MAP_EVENT,      /* appendMapEvent(type,arg) equivalent */
    NE_MISSILE_FIRED,
    NE_HIT,
    NE_DESTROYED,
    NE_LANDED,
    NE_EJECTED,
    NE_MISSION_END,
    NE_VIEW_HINT, /* director-style "switch view" suggestion */
};

#endif /* F15_NET_PROTOCOL_H */
