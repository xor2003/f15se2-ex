# Cut the air bridge

Sortie id: `airbase_and_logistics`
Phase: `3`
Player aircraft: captured F-15 or custom Soviet strike aircraft GLB slot

Hit Da Nang and Haiphong support targets to reduce USA sortie tempo.

## Objectives

- `strike_da_nang_airbase`: Da Nang airbase
  Airbase strike target for runway, fuel, and aircraft concentration.
  Target: `runway_and_fuel_site`, threat `high`.
  Desired effect: reduce USA sortie tempo from the coastal airbase
  Suggested loadout: runway cratering bombs plus self-defense missiles
  Success criteria: runway or airbase object destroyed; secondary fuel/logistics object destroyed if placed
- `interdict_haiphong_logistics`: Haiphong logistics
  Depot, bridge, port, or logistics interdiction target.
  Target: `port_depot_bridge_cluster`, threat `medium`.
  Desired effect: slow USA resupply and coastal reinforcement
  Suggested loadout: medium bombs or rockets with fuel margin for egress
  Success criteria: depot/bridge-equivalent object destroyed; escape route remains open after attack

## Authoring notes

Edit this Markdown file for human-facing campaign text.
Keep objective ids aligned with campaign.json and SVN.WLD.json if you rename them.
