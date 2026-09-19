# Implementation Backlog Adjusted To Current Code

This backlog is grounded in the current `f15se2-ex` code shape. The project is
not a blank rewrite: it already has a fixed-step simulation loop, SDL input
pump, gamepad bindings, tactical replay/debrief records, audio-event plumbing,
campaign asset hooks, and OpenGL/cockpit work. At the same time, much gameplay
logic still lives in decompiled global-state code, so large features should be
introduced through small explicit boundaries rather than by adding more special
cases to key handlers or frame code.

Relevant current anchors:

- `src/input.c`: one SDL event pump, BIOS-style key ring, gamepad menu and
  flight bindings.
- `src/egkeys.c`: flight command dispatch still operates on legacy key words.
- `src/egsys.c`: fixed-rate simulation loop with render interpolation.
- `src/egframe.c`: mission initialization, bullets, mission completion, and
  compact replay event recording.
- `src/enbrief.c`: debrief map, flight-path animation, and event sprites.
- `src/egcombat.c`, `src/egtarget.c`, `src/egthreat.c`: weapons, target locks,
  object destruction, and threat logic.
- `src/comm.h`, `src/worldxfer.c`: shared state handoff between former DOS
  program phases.

## Main Corrections

### Move Co-op Out Of The First Playable

The first playable milestone should not include two-player co-op. There is no
network/session layer in the current gameplay code, and the simulation is still
organized around one process and many globals. Co-op depends on stable action
messages, stable object identifiers, authoritative host state, and event-log
reconciliation. Those foundations should be built first in single-player.

### Treat Gamepad Support As Partly Started

Gamepad support already exists for menus and flight by feeding BIOS-style key
words into the same input ring as the keyboard. That is useful, but it is not yet
a shared action system. The next step is an explicit `GameAction` layer:

- Keyboard maps to actions.
- Gamepad maps to actions.
- Touch and future cockpit controls map to actions.
- Existing legacy key words become one compatibility producer, not the canonical
  gameplay API.

This avoids future cockpit controls, touch buttons, and remapping all becoming
separate parallel implementations of firing, target selection, gear, radar, and
autopilot.

### Build Debrief On The Existing Replay Path

The current code already writes compact replay/map events into `g_replayLog` and
the debrief already renders path/event data. Feature work should extend this
path instead of replacing it.

The missing layer is a richer mission event record:

- simulation time,
- event kind,
- actor id,
- target id,
- position,
- cause when known,
- objective state,
- final aircraft condition.

The existing compact replay can remain the debrief-map compatibility format, with
the richer log used for modern debrief text, saved debrief files, radio, and
future networking.

### Split Effects From Combat Outcomes

Weapon visual/audio effects should be consumers of simulation events, not causes
of damage or collision. Collision, damage, and detection must remain in the sim
path. Rendering quality settings should only change presentation detail.

### Build One Convincing Single-player Sortie First

Before adding new aircraft lists, campaigns, or co-op, build a single-player
reference sortie with enough behavior to prove the loop:

- game actions work across input devices,
- weapons and warnings are legible,
- nonfatal aircraft damage can happen,
- a convoy objective runs without depending on camera/rendering,
- debrief accurately explains what happened.

## Adjusted Backlog

### 1. Shared Game Actions And Bindings

Priority: P0  
Complexity: M

Implement a `GameAction` layer for all player commands: fire gun, fire selected
weapon, select weapon, target next, gear, brakes, thrust up/down, radar range,
countermeasures, view changes, autopilot, pause/menu, confirm, and back.

Keyboard, gamepad, touch, and cockpit controls should emit these actions. The
old BIOS key ring can remain as a compatibility input source while gameplay
moves toward actions.

Definition of done:

- Existing keyboard behavior is preserved.
- Existing gamepad behavior is preserved or improved.
- Menu and flight controls display binding names from the action binding table.
- Remapping and pitch inversion persist across restart.
- Firing and weapon selection behave identically from keyboard, gamepad, touch,
  and cockpit-control callers.

### 2. Mission Event Log And Saved Debrief

Priority: P0  
Complexity: M

Extend the current replay/debrief system with structured mission events. Keep
the existing compact map-event data for compatibility, but record richer events
for modern debrief and future systems.

Record:

- weapon launches,
- hits and misses where known,
- damage,
- destruction,
- objective changes,
- ejection,
- crash,
- landing,
- route sample once per simulation second.

Definition of done:

- A sortie containing launch, hit, damage, destruction, and landing shows each
  event once in simulation order.
- Reopening a saved debrief gives the same results.
- Unknown causes are displayed as unknown.
- Existing debrief map/path behavior remains functional.

### 3. Clear Weapon Effects And Threat Warnings

Priority: P0  
Complexity: M

Drive cannon, missile, impact, and warning presentation from simulation events.
Do not let visual effects alter collision, damage, targeting, or detection.

Audio warning priority:

1. incoming missile,
2. radar lock,
3. radar search.

Definition of done:

- Controlled scenarios produce each warning and effect.
- Misses do not produce hit effects.
- Rendering frame rate does not duplicate shots or impacts.
- Low/high effects settings produce the same combat outcomes.

### 4. Reference Single-player Convoy Sortie

Priority: P0  
Complexity: M to L

Create one reference mission with:

- friendly convoy following predefined waypoints,
- enemy ground group attacking the convoy,
- enemy air patrol that can detect and engage aircraft,
- objective success if at least one convoy vehicle reaches the destination,
- objective failure if all convoy vehicles are destroyed.

This should be single-player first. It should use the existing sim object and
weapon/detection rules wherever possible.

Definition of done:

- Without player intervention, groups still move/fight.
- Player intervention changes convoy losses or outcome.
- Destroying enemy attackers reduces attacks.
- Destroying the air patrol removes that threat.
- Camera position and render frame rate do not affect group activity.
- The flight does not end immediately when the objective resolves.

### 5. Short Interactive Training Missions

Priority: P0  
Complexity: M

Implement training as scenario scripts/checkpoints on top of the action layer
and event/objective state.

Lessons:

- takeoff,
- missile attack,
- missile avoidance,
- landing.

Definition of done:

- Each lesson advances only when the required action and result occur.
- Retry restores ammunition, fuel, aircraft condition, and target state.
- Lessons work with keyboard, gamepad, and touch.
- Wrong actions do not advance the lesson.

### 6. Event-driven Radio Messages

Priority: P1  
Complexity: M

Build radio messages as consumers of mission events and detection state. This
should replace or sideline random/director-only messages where appropriate.

Definition of done:

- Order, threat, objective, assistance, and damage events produce messages.
- Messages identify the speaker and show subtitles.
- Urgent messages interrupt routine messages.
- Identical speaker/message pairs are suppressed for 15 seconds.
- Messages about gone threats are discarded.
- Messages never reveal undetected enemies.

### 7. Recoverable Aircraft Damage

Priority: P1  
Complexity: L

Add an optional modern damage model while preserving original gameplay as a mode.
Start with an explicit aircraft damage state and then implement:

- engine failure,
- radar failure,
- reduced pitch/roll authority.

Definition of done:

- Each failure can be triggered separately.
- Single-engine failure does not destroy the aircraft.
- Radar failure clears existing locks and prevents new radar locks.
- Control damage reduces max authority to 50 percent.
- Damaged aircraft can land.
- Debrief reports objective result, safe return, and aircraft damage separately.

### 8. Useful AI Wingman Commands

Priority: P1  
Complexity: L

Do after action binding, target-state cleanup, and at least one objective mission
exist. Implement one active order per wingman:

- cover me,
- attack selected target,
- rejoin,
- return to base.

Definition of done:

- Orders work in one mission.
- Rejoin cancels an active attack.
- Impossible orders are rejected with a reason.
- Destroyed targets are cleared from active orders.
- The map displays current wingman order.

### 9. Small Persistent Operation

Priority: P2  
Complexity: L

Use the current campaign/world JSON direction, but add a separate operation-save
state rather than overloading the small in-memory `GameComm` handoff.

Start with three linked sorties and persistent designated objects:

- destroyed radar remains destroyed,
- destroyed allied units are unavailable later,
- surviving allied units can appear later.

Definition of done:

- Both success and failure advance the operation.
- Changes commit only when the player selects Continue on the debrief.
- Continue is idempotent.
- Save/restart preserves consequences.
- The next briefing explains relevant consequences.

### 10. Functional Controls In The 3D Cockpit

Priority: P2  
Complexity: M after cockpit model/control metadata exists

Make cockpit controls call the same `GameAction` layer:

- landing gear,
- flaps,
- weapon selection,
- radar mode,
- autopilot,
- display page.

Definition of done:

- Cockpit controls produce the same result as keyboard/gamepad actions.
- Shortcuts update cockpit switch state.
- Gamepad cockpit-interaction mode does not also steer the aircraft.
- Disabled controls show a reason such as radar damage.

### 11. Two-player Cooperative Missions

Priority: P3  
Complexity: XL

Do not include this in the first playable. Implement after action, event,
objective, and damage systems have stable boundaries.

Version-one scope:

- LAN/direct-IP only,
- host authoritative simulation,
- separate aircraft and loadouts,
- shared target markers,
- spectators after destruction/ejection,
- guest disconnect does not award a kill,
- host disconnect ends the session,
- menus do not pause unless the host explicitly pauses.

Definition of done:

- Desktop host and Android guest complete the reference convoy mission.
- Results are consistent under 100 ms RTT and 1 percent packet loss.
- No duplicate kills, missile launches, or objective changes occur.

## Revised First Playable Milestone

Build a single-player convincing sortie before co-op.

Include:

- shared `GameAction` controls,
- clear weapon effects and threat warnings,
- mission event log and saved debrief,
- recoverable damage v1,
- one convoy defense sortie.

Acceptance scenario:

The player launches, supports a moving convoy, reacts to an air or ground threat,
takes or avoids damage, returns to base if possible, and receives a debrief that
accurately explains objective result, losses, route, weapon events, and aircraft
condition.

After this works, co-op becomes a networked version of a proven gameplay loop
instead of an attempt to solve controls, objectives, debrief, damage, and netcode
all at once.
