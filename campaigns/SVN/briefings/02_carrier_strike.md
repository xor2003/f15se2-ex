# Strike the carrier group

Sortie id: `carrier_strike`
Phase: `2`
Player aircraft: captured F-15 with Soviet/Vietnamese ground control

Attack the USA carrier group while fighter patrols are displaced over the Gulf of Tonkin.

## Objectives

- `strike_carrier_group`: USA carrier group
  Main naval strike target in the Gulf of Tonkin.
  Target: `carrier_task_force`, threat `very_high`.
  Desired effect: disable carrier flight operations
  Suggested loadout: anti-ship / heavy strike ordnance with fighter cover
  Success criteria: primary ship or carrier-equivalent object destroyed; at least one escort/radar object suppressed if present
- `sweep_tonkin_patrol`: Tonkin patrol
  USA patrol aircraft / CAP encounter area.
  Target: `fighter_patrol_area`, threat `medium_high`.
  Desired effect: draw USA fighters away from the carrier strike lane
  Suggested loadout: air-to-air missiles and external fuel
  Success criteria: enemy patrol aircraft destroyed or displaced; carrier strike route remains clear

## Authoring notes

Edit this Markdown file for human-facing campaign text.
Keep objective ids aligned with campaign.json and SVN.WLD.json if you rename them.
