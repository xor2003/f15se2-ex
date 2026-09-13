# SVN mission target plan

This file is generated for campaign authors. Edit authoritative values in `campaign.json` and the WLD JSON, then re-run `package-campaign`.

## Runtime target selection

The game currently uses the selected sortie's `primary_objective_ids` and `secondary_objective_ids` as mission target hints.
The legacy generator still validates the chosen target against preserved mission tables, distance, bases, and fallback behavior.

## Objectives and WLD slots

- `start_soviet_hq` slot `0`: WLD `Soviet forward HQ`, legacy `SOVIET HQ` at `27256,4893` - Soviet forward HQ [soviet_vietnamese/base/defend]
  Target type: `friendly_command_post`; threat: `friendly`.
  Desired effect: preserve
  Success criteria:
  - HQ remains available as sortie staging point
  Radio cues: `voice_cue_000_sample0`.
- `strike_carrier_group` slot `6`: WLD `USA carrier group`, legacy `SAM Radar` at `28300,10979` - USA carrier group [usa/naval/strike]
  Target type: `carrier_task_force`; threat: `very_high`.
  Desired effect: disable carrier flight operations
  Success criteria:
  - primary ship or carrier-equivalent object destroyed
  - at least one escort/radar object suppressed if present
  Radio cues: `voice_cue_002_sample2_variant0`.
- `strike_da_nang_airbase` slot `7`: WLD `Da Nang airbase`, legacy `SAM Radar` at `21358,20256` - Da Nang airbase [usa/airbase/strike]
  Target type: `runway_and_fuel_site`; threat: `high`.
  Desired effect: reduce USA sortie tempo from the coastal airbase
  Success criteria:
  - runway or airbase object destroyed
  - secondary fuel/logistics object destroyed if placed
  Radio cues: `voice_cue_003_sample2_variant1`.
- `suppress_hanoi_radar` slot `3`: WLD `Hanoi radar net`, legacy `SAM Radar` at `27269,4750` - Hanoi radar net [usa_allied/air_defense/suppress]
  Target type: `sam_radar_network`; threat: `high`.
  Desired effect: open a low-risk corridor toward Hanoi and Haiphong
  Success criteria:
  - radar object destroyed or moved out of active route
  - SAM/AAA object destroyed if paired with the radar
  Radio cues: `voice_cue_001_sample4, voice_cue_004_sample2_variant2`.
- `interdict_haiphong_logistics` slot `4`: WLD `Haiphong logistics`, legacy `SAM Radar` at `28384,6972` - Haiphong logistics [usa_allied/logistics/interdict]
  Target type: `port_depot_bridge_cluster`; threat: `medium`.
  Desired effect: slow USA resupply and coastal reinforcement
  Success criteria:
  - depot/bridge-equivalent object destroyed
  - escape route remains open after attack
  Radio cues: `voice_cue_004_sample2_variant2`.
- `sweep_tonkin_patrol` slot `5`: WLD `Tonkin patrol`, legacy `SAM Radar` at `28523,9132` - Tonkin patrol [usa/air/sweep]
  Target type: `fighter_patrol_area`; threat: `medium_high`.
  Desired effect: draw USA fighters away from the carrier strike lane
  Success criteria:
  - enemy patrol aircraft destroyed or displaced
  - carrier strike route remains clear
  Radio cues: `voice_cue_002_sample2_variant0`.

## Sorties

- `opening_sead` phase `1`: Open the radar corridor
  Launch selector: `--campaign-sortie opening_sead` or `--campaign-sortie 1`
  Primary: `suppress_hanoi_radar`
  Secondary: ``
  START-board briefing: Destroy or suppress the Hanoi radar net so Lisicin can move deeper into the theater.
- `carrier_strike` phase `2`: Strike the carrier group
  Launch selector: `--campaign-sortie carrier_strike` or `--campaign-sortie 2`
  Primary: `strike_carrier_group`
  Secondary: `sweep_tonkin_patrol`
  START-board briefing: Attack the USA carrier group while fighter patrols are displaced over the Gulf of Tonkin.
- `airbase_and_logistics` phase `3`: Cut the air bridge
  Launch selector: `--campaign-sortie airbase_and_logistics` or `--campaign-sortie 3`
  Primary: `strike_da_nang_airbase`
  Secondary: `interdict_haiphong_logistics`
  START-board briefing: Hit Da Nang and Haiphong support targets to reduce USA sortie tempo.

## Target packages

### `target_set_hanoi_corridor`

- Sortie: `opening_sead` / Open the radar corridor
- Role: SEAD / ingress corridor opening
- Attack order: `search_radar, sam_launcher, aaa_screen`
- Victory logic: `{"friendly_survival_required": ["start_soviet_hq"], "required_primary_destroyed": 1, "required_secondary_destroyed": 0}`
- Editor notes: Place paired radar/SAM/AAA objects around Hanoi; keep one clear low-altitude return lane to HQ.

Target elements:

- `search_radar` -> `suppress_hanoi_radar` (Hanoi radar net): primary; weapon `anti-radiation missile or precision bomb`; effect blind the radar belt.
  Placement: Place on or near the Hanoi radar waypoint; this is the runtime primary target slot.
  Defense behavior: Should cue SAM/AAA objects and be visible early enough for a SEAD pass.
- `sam_launcher` -> `suppress_hanoi_radar` (Hanoi radar net): secondary; weapon `bombs/rockets`; effect reduce missile threat during egress.
  Placement: Place 1-3 nearby launcher objects offset from the radar so one bomb run cannot erase the whole site.
  Defense behavior: Threat should punish high-altitude direct ingress but leave low-altitude masking viable.
- `aaa_screen` -> `suppress_hanoi_radar` (Hanoi radar net): optional; weapon `cannon/rockets`; effect make repeated passes survivable.
  Placement: Place around the radar perimeter or along the egress lane.
  Defense behavior: Short-range pressure only; avoid making the first sortie impossible.

### `target_set_tonkin_carrier`

- Sortie: `carrier_strike` / Strike the carrier group
- Role: fighter sweep followed by anti-ship strike
- Attack order: `cap_patrol, escort_radar, carrier_deck`
- Victory logic: `{"bonus_objective_ids": ["sweep_tonkin_patrol"], "required_primary_destroyed": 1, "required_secondary_destroyed": 1}`
- Editor notes: Keep carrier and CAP separated enough that the player must choose sweep-first or high-risk direct strike.

Target elements:

- `cap_patrol` -> `sweep_tonkin_patrol` (Tonkin patrol): screen; weapon `air-to-air missiles`; effect open the approach lane.
  Placement: Place ahead of the carrier box, not directly on top of it, so sweep-first and bypass tactics differ.
  Defense behavior: Intercept player before anti-ship release; disengage pressure after carrier strike lane is opened.
- `escort_radar` -> `strike_carrier_group` (USA carrier group): secondary; weapon `anti-radiation or precision strike`; effect reduce fleet warning time.
  Placement: Place close to the carrier group as an escort/radar picket object.
  Defense behavior: Supports point defense and early warning; not required if using a simple one-object carrier target.
- `carrier_deck` -> `strike_carrier_group` (USA carrier group): primary; weapon `heavy bombs / anti-ship loadout`; effect disable carrier flight operations.
  Placement: Place at the Gulf of Tonkin carrier waypoint; this is the runtime primary target slot.
  Defense behavior: High-value target with escort coverage; should not require destroying every ship to complete the sortie.

### `target_set_southern_air_bridge`

- Sortie: `airbase_and_logistics` / Cut the air bridge
- Role: logistics interdiction plus airbase strike
- Attack order: `haiphong_depot, da_nang_runway, fuel_storage`
- Victory logic: `{"egress_waypoint": "wp_hq", "required_primary_destroyed": 1, "required_secondary_destroyed": 1}`
- Editor notes: This route is long; keep threats sparse or provide a custom aircraft/fuel balance in future runtime logic.

Target elements:

- `haiphong_depot` -> `interdict_haiphong_logistics` (Haiphong logistics): secondary; weapon `medium bombs`; effect slow coastal resupply.
  Placement: Place on the Haiphong logistics waypoint as a depot, bridge, port, or convoy-staging object.
  Defense behavior: Medium threat; should be optional if fuel or damage state makes Da Nang the safer priority.
- `da_nang_runway` -> `strike_da_nang_airbase` (Da Nang airbase): primary; weapon `runway cratering bombs`; effect stop USA sorties from Da Nang.
  Placement: Place at the Da Nang waypoint; this is the runtime primary target slot.
  Defense behavior: Defended by local AAA/SAM and possible fighters, but reachable without carrier-level escort density.
- `fuel_storage` -> `strike_da_nang_airbase` (Da Nang airbase): secondary; weapon `bombs/rockets`; effect increase recovery time after runway repair.
  Placement: Place near but not overlapping the runway target so the player must choose a second pass.
  Defense behavior: Soft secondary target; useful for score/campaign effect rather than hard mission completion.

## Route anchors

- `wp_hq` -> `start_soviet_hq`: Gia Lam / Hanoi forward control at WLD `27256,4893`
- `wp_hanoi_radar` -> `suppress_hanoi_radar`: Hanoi SAM radar belt at WLD `27269,4750`
- `wp_haiphong` -> `interdict_haiphong_logistics`: Haiphong port logistics at WLD `28384,6972`
- `wp_tonkin_cap` -> `sweep_tonkin_patrol`: Gulf of Tonkin CAP lane at WLD `28523,9132`
- `wp_carrier` -> `strike_carrier_group`: Gulf of Tonkin carrier box at WLD `28300,10979`
- `wp_danang` -> `strike_da_nang_airbase`: Da Nang airbase at WLD `21358,20256`
