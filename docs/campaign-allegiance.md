# Campaign allegiance

Optional `campaign.json` fields use zero-based WLD array slots:

```json
"friendly_flight_slots": [2, 5],
"friendly_base_slots": [27, 28]
```

Listed flights do not pursue the player through the threat-alert path or fire
at the player. The final four flight slots are populated from nearby bases;
their friendliness comes from their current base's world-object slot instead.
Aircraft model IDs do not determine allegiance.

Omitted fields leave the original threat rules unchanged. Unlisted units also
retain those rules; this is not a new faction-versus-faction combat simulation.
Ground-unit allegiance continues to use the existing WLD flags. Update slot lists
when reordering WLD objects or flights. Load the manifest with `--campaign`;
selecting only a scenario does not load these overrides.
