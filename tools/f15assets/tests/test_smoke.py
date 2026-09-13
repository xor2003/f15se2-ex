from __future__ import annotations

import base64
import contextlib
import io
import pathlib
import json
import shutil
import struct
import tempfile
import unittest
import wave
import zipfile

from f15assets import decode_pic_asset, encode_pic_asset, export_3d3_to_gltf, export_3d3_to_glb, export_3d3_shape_gltfs, export_3d3_gltf_to_glb
from f15assets import parse_3d3, build_3d3, parse_3dg, build_3dg, parse_3dt, build_3dt, parse_wld, build_wld
from tools.f15assets import cli as cli_module


class SmokeConvertersTest(unittest.TestCase):
    def test_pic_roundtrip_byte_mode(self):
        pixels = bytes(((i * 7) % 256) for i in range(320 * 200))
        payload = {
            "format": "PIC",
            "decoded_width": 320,
            "decoded_height": 200,
            "max_lzw_width": 12,
            "bitstream_mode": "byte",
            "pixels_base64": base64.b64encode(pixels).decode("ascii"),
        }

        encoded = encode_pic_asset(payload)
        decoded = decode_pic_asset(encoded)
        out = base64.b64decode(decoded["pixels_base64"].encode("ascii"))
        self.assertEqual(out[: len(pixels)], pixels)

    def test_pic_roundtrip_nibble_mode(self):
        nibble_pixels = bytes(((i * 5) & 0x0F) for i in range(320 * 200))
        payload = {
            "format": "PIC",
            "decoded_width": 320,
            "decoded_height": 200,
            "max_lzw_width": 11,
            "bitstream_mode": "nibble",
            "pixels_base64": base64.b64encode(nibble_pixels).decode("ascii"),
        }

        encoded = encode_pic_asset(payload)
        decoded = decode_pic_asset(encoded)
        out = base64.b64decode(decoded["pixels_base64"].encode("ascii"))
        self.assertEqual(out[: len(nibble_pixels)], nibble_pixels)

    def test_pic_roundtrip_preserves_compressed_payload(self):
        payload = {
            "format": "PIC",
            "decoded_width": 320,
            "decoded_height": 200,
            "max_lzw_width": 12,
            "bitstream_mode": "byte",
            "pixels_base64": base64.b64encode(bytes(((i * 3) % 256) for i in range(320 * 200))).decode("ascii"),
        }

        encoded = encode_pic_asset(payload)
        decoded = decode_pic_asset(encoded)
        rebuilt = encode_pic_asset(decoded)
        self.assertEqual(encoded, rebuilt)

    def test_pic_sidecar_includes_palette_profile(self):
        payload = {
            "format": "PIC",
            "decoded_width": 320,
            "decoded_height": 200,
            "max_lzw_width": 12,
            "bitstream_mode": "byte",
            "pixels_base64": base64.b64encode(bytes((i & 0xFF) for i in range(320 * 200))).decode("ascii"),
        }
        encoded = encode_pic_asset(payload)
        decoded = decode_pic_asset(encoded)
        self.assertEqual(decoded["format"], "PIC")
        palette_profile = decoded.get("palette_profile")
        self.assertIsInstance(palette_profile, dict)
        self.assertEqual(palette_profile.get("status"), "not_embedded")
        self.assertEqual(palette_profile.get("index_mode"), "indexed")
        self.assertEqual(palette_profile.get("index_bit_depth"), 8)

        nibble_payload = {
            "format": "PIC",
            "decoded_width": 320,
            "decoded_height": 200,
            "max_lzw_width": 11,
            "bitstream_mode": "nibble",
            "pixels_base64": base64.b64encode(bytes(((i * 3) & 0x0F) for i in range(320 * 200))).decode("ascii"),
        }
        nibble_encoded = encode_pic_asset(nibble_payload)
        nibble_decoded = decode_pic_asset(nibble_encoded)
        palette_profile = nibble_decoded.get("palette_profile")
        self.assertIsInstance(palette_profile, dict)
        self.assertEqual(palette_profile.get("status"), "not_embedded")
        self.assertEqual(palette_profile.get("index_bit_depth"), 4)

    def test_cli_rejects_invalid_flag_combinations(self):
        sample_pixels = bytes((i & 0xFF) for i in range(320 * 200))
        pic_bytes = encode_pic_asset(
            {
                "format": "PIC",
                "decoded_width": 320,
                "decoded_height": 200,
                "max_lzw_width": 12,
                "bitstream_mode": "byte",
                "pixels_base64": base64.b64encode(sample_pixels).decode("ascii"),
            }
        )
        shape_payload = build_3d3(
            {
                "format": "3D3",
                "shape_offsets": [0],
                "model_data": "",
                "model_data_size": 0,
                "trailing_bytes": "",
            }
        )

        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            pic_path = base / "sample.pic"
            grid_path = base / "sample.3D3"
            table_path = base / "sample.3DT"

            pic_path.write_bytes(pic_bytes)
            grid_path.write_bytes(shape_payload)
            table_path.write_bytes(
                build_3dt(
                    {
                        "format": "3DT",
                        "version": 1,
                        "levels": [
                            {"level": 0, "objects": []},
                            {"level": 1, "objects": []},
                            {"level": 2, "objects": []},
                            {"level": 3, "objects": []},
                            {"level": 4, "objects": []},
                        ],
                    }
                )
            )

            rc = cli_module.main(
                ["decode", str(grid_path), str(base / "out.json"), "--png", str(base / "out.png")]
            )
            self.assertEqual(rc, 1)

            rc = cli_module.main(
                [
                    "decode",
                    str(pic_path),
                    str(base / "out2.json"),
                    "--gltf",
                    str(base / "out2.gltf"),
                ]
            )
            self.assertEqual(rc, 1)

            rc = cli_module.main(
                [
                    "decode",
                    str(table_path),
                    str(base / "out3dt.json"),
                    "--png",
                    str(base / "out3dt.png"),
                ]
            )
            self.assertEqual(rc, 1)

    def test_3dt_roundtrip(self):
        payload = {
            "format": "3DT",
            "version": 1,
            "levels": [
                {
                    "level": 0,
                    "objects": [
                        {
                            "tile_index": 0,
                            "objects": [{"x": 10, "y": 20, "z": -5, "shape_word": 0x12FF}],
                        }
                    ],
                },
                {"level": 1, "objects": []},
                {"level": 2, "objects": []},
                {"level": 3, "objects": []},
                {"level": 4, "objects": []},
            ],
            "trailing_bytes": base64.b64encode(b"tail").decode("ascii"),
        }
        encoded = build_3dt(payload)
        parsed = parse_3dt(encoded)
        self.assertEqual(parsed["tile_counts"][0], 1)
        self.assertEqual(parsed["levels"][0]["objects"][0]["objects"][0]["shape_word"], 0x12FF)
        self.assertEqual(parsed["trailing_bytes"], payload["trailing_bytes"])
        self.assertEqual(build_3dt(parsed), encoded)

    def test_3dg_roundtrip(self):
        payload = {
            "format": "3DG",
            "version": 1,
            "level4_top_grid": [0] * 16,
            "level3_grid": [0x11] * 256,
            "level2_subgrid": [0x22] * 512,
            "level1_subgrid": [0x33] * 512,
            "level0_subgrid": [0x44] * 512,
            "trailing_bytes": base64.b64encode(b"tail").decode("ascii"),
        }
        encoded = build_3dg(payload)
        parsed = parse_3dg(encoded)
        self.assertEqual(parsed["level4_top_grid"][0], 0)
        self.assertEqual(parsed["level3_grid"][0], 0x11)
        self.assertEqual(parsed["trailing_bytes"], payload["trailing_bytes"])
        self.assertEqual(build_3dg(parsed), encoded)

    def test_3d3_roundtrip_without_tail(self):
        payload = {
            "format": "3D3",
            "shape_offsets": [0, 6, 12],
            "model_data": base64.b64encode(b"\x00" * 12 + b"abc").decode("ascii"),
            "model_data_size": 15,
            "shared_vertex_pool": None,
            "trailing_bytes": "",
        }
        encoded = build_3d3(payload)
        parsed = parse_3d3(encoded)
        self.assertEqual(parsed["shape_offsets"], [0, 6, 12])
        self.assertEqual(build_3d3(parsed), encoded)

    def test_wld_roundtrip(self):
        payload = {
            "format": "WLD",
            "terrain_target_ids": {"land": 0, "water": 0},
            "read_item_size": 2,
            "ground_unit_count": 0,
            "world_object_count": 1,
            "world_objects": [
                {
                    "unitRef": 1,
                    "x_coord": 100,
                    "y_coord": 200,
                    "unitType": 3,
                    "targetFlags": 0,
                    "occupantType": 4,
                    "patrolCount": 0,
                    "objectIdx": 7,
                },
                {
                    "unitRef": 2,
                    "x_coord": 300,
                    "y_coord": 400,
                    "unitType": 4,
                    "targetFlags": 1,
                    "occupantType": 5,
                    "patrolCount": 2,
                    "objectIdx": 8,
                },
            ],
            "flight_unit_count": 1,
            "flight_units": [
                {
                    "waypointIdx": 1,
                    "x": 10,
                    "y": 11,
                    "altitude": 12,
                    "xPrecise": 123,
                    "yPrecise": -456,
                    "heading": 0x1234,
                    "pitch": -2,
                    "roll": 3,
                    "planeType": 4,
                    "flags": 0,
                    "maxSpeed": 200,
                    "fuel": 999,
                    "weaponType": 0x0100,
                    "terrainColor": 0x0302,
                    "damage": 0x0504,
                }
            ],
            "shape_target_category_table": base64.b64encode(b"A" * 100).decode("ascii"),
            "kill_tally_or_unit_flags": base64.b64encode(b"B" * 100).decode("ascii"),
            "mission_object_type_table": base64.b64encode(b"C" * 100).decode("ascii"),
            "terrain_grid": base64.b64encode(bytes(range(256))).decode("ascii"),
            "name_table": base64.b64encode(b"name_one\x00name_two\x00" + b"\x00" * 732).decode("ascii"),
            "trailing_bytes": base64.b64encode(b"wld-tail").decode("ascii"),
        }
        encoded = build_wld(payload)
        parsed = parse_wld(encoded)
        self.assertEqual(base64.b64decode(parsed["shape_target_category_table"].encode("ascii"))[:3], b"AAA")
        self.assertEqual(parsed["flight_units"][0]["xPrecise"], 123)
        self.assertEqual(parsed["trailing_bytes"], payload["trailing_bytes"])
        self.assertEqual(build_wld(parsed), encoded)

    def test_wld_roundtrip_with_short_name_table(self):
        payload = {
            "format": "WLD",
            "terrain_target_ids": {"land": 0x11, "water": 0x22},
            "read_item_size": 0,
            "ground_unit_count": 0,
            "world_object_count": 0,
            "world_objects": [],
            "flight_unit_count": 0,
            "flight_units": [],
            "shape_target_category_table": base64.b64encode(b"\x01" * 100).decode("ascii"),
            "kill_tally_or_unit_flags": base64.b64encode(b"\x02" * 100).decode("ascii"),
            "mission_object_type_table": base64.b64encode(b"\x03" * 100).decode("ascii"),
            "terrain_grid": base64.b64encode(bytes(range(256))).decode("ascii"),
            "name_table": base64.b64encode(b"JP\x00WLD\x00").decode("ascii"),
            "trailing_bytes": "",
        }
        encoded = build_wld(payload)
        parsed = parse_wld(encoded)
        self.assertEqual(parsed["name_table"], payload["name_table"])
        self.assertEqual(parsed["name_strings"], ["JP", "WLD"])
        self.assertEqual(parsed["trailing_bytes"], "")
        self.assertEqual(build_wld(parsed), encoded)

    def test_cli_new_campaign_scaffolds_soviet_vietnam_wld(self):
        payload = {
            "format": "WLD",
            "terrain_target_ids": {"land": 0, "water": 0},
            "read_item_size": 6,
            "ground_unit_count": 6,
            "world_object_count": 6,
            "world_objects": [
                {
                    "unitRef": i,
                    "x_coord": 100 + i,
                    "y_coord": 200 + i,
                    "unitType": 1,
                    "targetFlags": 0,
                    "occupantType": 0,
                    "patrolCount": 0,
                    "objectIdx": i,
                }
                for i in range(6)
            ],
            "flight_unit_count": 0,
            "flight_units": [],
            "shape_target_category_table": base64.b64encode(bytes([1] * 100)).decode("ascii"),
            "kill_tally_or_unit_flags": base64.b64encode(bytes([2] * 100)).decode("ascii"),
            "mission_object_type_table": base64.b64encode(bytes([0, 1, 2, 3, 4, 5] + [0] * 94)).decode("ascii"),
            "terrain_grid": base64.b64encode(bytes(range(256))).decode("ascii"),
            "name_table": base64.b64encode(b"OLD0\x00OLD1\x00OLD2\x00OLD3\x00OLD4\x00OLD5\x00").decode("ascii"),
            "name_strings": ["OLD0", "OLD1", "OLD2", "OLD3", "OLD4", "OLD5"],
            "trailing_bytes": "",
        }
        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            template = base / "VN.WLD.json"
            output = base / "converted_assets_all"
            template.write_text(json.dumps(payload), encoding="utf-8")

            rc = cli_module.main(["new-campaign", str(template), str(output)])
            self.assertEqual(rc, 0)

            campaign_path = output / "SVN" / "SVN.WLD.json"
            manifest_path = output / "SVN" / "campaign.json"
            readme_path = output / "SVN" / "README.md"
            summary_path = output / "SVN" / "SUMMARY.md"
            inventory_path = output / "SVN" / "inventory.json"
            self.assertTrue(campaign_path.exists())
            self.assertTrue(manifest_path.exists())
            self.assertTrue(readme_path.exists())
            self.assertTrue(summary_path.exists())
            self.assertTrue(inventory_path.exists())
            generated = json.loads(campaign_path.read_text(encoding="utf-8"))
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
            self.assertEqual(manifest["format"], "F15SE2_CAMPAIGN")
            self.assertEqual(manifest["scenario_schema"], "F15SE2_MODERN_CAMPAIGN_WLD")
            self.assertEqual(manifest["scenario_schema_version"], 1)
            self.assertEqual(manifest["campaign_id"], "SVN")
            self.assertEqual(manifest["display_name"], manifest["title"])
            self.assertEqual(manifest["world"], "SVN.WLD.json")
            self.assertEqual(manifest["runtime_selection"]["scenario_base"], "VN")
            self.assertEqual(manifest["launch"]["argv"], ["--campaign", "SVN"])
            self.assertEqual(manifest["launch"]["environment"]["F15_REPLACEMENT_ROOT"], "converted_assets_all")
            self.assertEqual(manifest["media_policy"]["images"]["source_of_truth"], "PNG")
            self.assertEqual(manifest["media_policy"]["images"]["scaling"], "fit_original_game_rectangle")
            self.assertEqual(
                manifest["objective_capabilities"]["custom_world_scenario"]["runtime_modes"],
                ["campaign", "campaign_sortie", "raw_scenario"],
            )
            self.assertEqual(manifest["objective_capabilities"]["modern_png_images"]["source_format"], "PNG")
            self.assertIn("source_color_count", manifest["objective_capabilities"]["modern_png_images"]["limits_removed"])
            self.assertEqual(manifest["objective_capabilities"]["modern_wav_radio"]["language"], "Russian")
            self.assertFalse(manifest["objective_capabilities"]["modern_fonts"]["provided"])
            self.assertIn("fonts/font_1.ttf", manifest["objective_capabilities"]["modern_fonts"]["override_slots"])
            self.assertEqual(manifest["objective_capabilities"]["modern_glb_models"]["source_format"], "GLB")
            self.assertEqual(manifest["objective_capabilities"]["mission_targets"]["objective_count"], 6)
            self.assertEqual(manifest["objective_capabilities"]["mission_targets"]["target_set_count"], 3)
            self.assertEqual(manifest["verification_status"]["runtime_launch_validation"], "not_recorded_by_generator")
            self.assertEqual(manifest["verification_status"]["full_asset_replacement_validation"], "not_recorded_by_generator")
            self.assertIn("campaign-local per-shape GLB visual model replacements", manifest["runtime_contract"]["modern_editable"])
            self.assertIn("aircraft flight model, weapons, AI, and hardcoded behavior tables", manifest["runtime_contract"]["legacy_driven"])
            self.assertEqual(manifest["font_overrides"][0]["file"], "fonts/font_1.ttf")
            readme_text = readme_path.read_text(encoding="utf-8")
            self.assertIn("Cyrillic", readme_text)
            self.assertIn("strike_carrier_group", readme_text)
            self.assertIn("USA carrier group", readme_text)
            self.assertIn("carrier_strike", readme_text)
            self.assertIn("Launch metadata", readme_text)
            self.assertIn("Modern media policy", readme_text)
            self.assertIn("Objective capability map", readme_text)
            self.assertIn("Verification status", readme_text)
            self.assertIn("fit_original_game_rectangle", readme_text)
            self.assertIn("Runtime contract", readme_text)
            self.assertIn("Asset license review", readme_text)
            self.assertIn("Editable briefing files", readme_text)
            self.assertIn("Route plan", readme_text)
            self.assertIn("Routes on", readme_text)
            self.assertIn("Mission target packages", readme_text)
            self.assertIn("inventory.json", readme_text)
            self.assertIn("SUMMARY.md", readme_text)
            summary_text = summary_path.read_text(encoding="utf-8")
            self.assertIn("Li Si Cin", summary_text)
            self.assertIn("Route plan and real-world anchors", summary_text)
            self.assertIn("target_set_tonkin_carrier", summary_text)
            self.assertIn("Edit workflow", summary_text)
            self.assertIn("refresh-campaign-inventory", summary_text)
            self.assertIn("Edit workflow", readme_text)
            self.assertEqual(inventory["format"], "F15SE2_CAMPAIGN_INVENTORY")
            self.assertEqual(inventory["campaign_id"], "SVN")
            self.assertEqual(inventory["display_name"], manifest["display_name"])
            self.assertTrue(any(item["file"] == "campaign.json" for item in inventory["files"]))
            self.assertTrue(any(item["file"] == "SUMMARY.md" for item in inventory["files"]))
            self.assertTrue(any(item["file"] == "run_campaign.sh" for item in inventory["files"]))
            campaign_manifest_item = next(item for item in inventory["files"] if item["file"] == "campaign.json")
            self.assertEqual(len(campaign_manifest_item["sha256"]), 64)
            self.assertGreater(campaign_manifest_item["bytes"], 0)
            launcher_text = (output / "SVN" / "run_campaign.sh").read_text(encoding="utf-8")
            self.assertIn("--campaign SVN", launcher_text)
            self.assertIn("F15_REPLACEMENT_ROOT", launcher_text)
            rc = cli_module.main([
                "record-campaign-verification",
                str(output / "SVN"),
                "--runtime-launch",
                "passed",
                "--runtime-launch-command",
                "./f15se2-ex --game /tmp/F15_GAME --campaign SVN",
                "--replacement-loadability",
                "passed",
                "--replacement-loadability-command",
                "python3 tools/f15assets/cli.py validate-replacements /tmp/F15_GAME converted_assets_all --loadability-only",
                "--full-asset-validation",
                "passed",
                "--full-asset-validation-command",
                "python3 tools/f15assets/cli.py validate-replacements /tmp/F15_GAME converted_assets_all",
                "--notes",
                "smoke-test evidence",
            ])
            self.assertEqual(rc, 0)
            recorded_manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(recorded_manifest["verification_status"]["runtime_launch_validation"], "passed")
            self.assertEqual(recorded_manifest["verification_status"]["replacement_loadability_validation"], "passed")
            self.assertEqual(recorded_manifest["verification_status"]["full_asset_replacement_validation"], "passed")
            self.assertEqual(recorded_manifest["verification_status"]["runtime_launch_validation_evidence"]["status"], "passed")
            self.assertTrue(recorded_manifest["verification_status"]["runtime_launch_validation_evidence"]["recorded_at"])
            self.assertTrue(recorded_manifest["verification_status"]["replacement_loadability_validation_evidence"]["recorded_at"])
            self.assertTrue(recorded_manifest["verification_status"]["full_asset_replacement_validation_evidence"]["recorded_at"])
            self.assertIn("--loadability-only", recorded_manifest["verification_status"]["replacement_loadability_validation_evidence"]["command"])
            self.assertIn("--campaign SVN", recorded_manifest["verification_status"]["runtime_launch_validation_evidence"]["command"])
            self.assertEqual(recorded_manifest["verification_status"]["latest_notes"], "smoke-test evidence")
            recorded_readme_text = readme_path.read_text(encoding="utf-8")
            self.assertIn("Evidence command", recorded_readme_text)
            self.assertEqual(manifest["campaign_art"][0]["file"], "TITLE640.png")
            self.assertEqual(manifest["campaign_art"][0]["width"], 3840)
            self.assertEqual(manifest["campaign_art"][0]["height"], 2100)
            self.assertIn(manifest["campaign_art"][0]["color_model"], {"RGB888", "RGBA8888"})
            self.assertEqual(manifest["campaign_art"][0]["replacement_contract"]["source_format"], "PNG")
            self.assertEqual(manifest["campaign_art"][0]["replacement_contract"]["fallback"], "TITLE640.PIC")
            self.assertEqual(manifest["campaign_art"][0]["license_status"], "generated_placeholder_needs_replacement_or_review")
            self.assertEqual(manifest["asset_license_review"]["status"], "required_before_free_asset_distribution")
            self.assertTrue((output / "SVN" / "TITLE640.png").exists())
            self.assertTrue((output / "SVN" / "TITLE.png").exists())
            self.assertTrue((output / "SVN" / "DESK.png").exists())
            self.assertTrue((output / "SVN" / "WALL.png").exists())
            self.assertTrue((output / "SVN" / "HISCORE.png").exists())
            self.assertTrue((output / "SVN" / "ARMPIECE.png").exists())
            self.assertTrue((output / "SVN" / "VN.png").exists())
            self.assertEqual(
                [item["replaces"] for item in manifest["campaign_art"]],
                [
                    "TITLE640.PIC",
                    "TITLE.PIC",
                    "DESK.PIC",
                    "WALL.PIC",
                    "HISCORE.PIC",
                    "ARMPIECE.PIC",
                    "VN.SPR",
                    "start/menu/arm/0.png",
                    "start/menu/arm/1.png",
                    "start/menu/arm/2.png",
                    "start/menu/arm/3.png",
                    "start/menu/arm/4.png",
                    "start/menu/arm/5.png",
                    "start/menu/arm/6.png",
                ],
            )
            self.assertEqual(len(manifest["mission_target_sets"]), 3)
            carrier_targets = next(item for item in manifest["mission_target_sets"] if item["id"] == "target_set_tonkin_carrier")
            self.assertEqual(carrier_targets["attack_order"], ["cap_patrol", "escort_radar", "carrier_deck"])
            self.assertEqual(carrier_targets["victory_logic"]["required_primary_destroyed"], 1)
            self.assertEqual(manifest["campaign_music"][0]["file"], "sounds/intro_music.asound.json")
            self.assertTrue((output / "SVN" / "sounds" / "intro_music.asound.json").exists())
            self.assertGreaterEqual(len(manifest["campaign_sounds"]), 5)
            self.assertEqual(manifest["objective_capabilities"]["modern_wav_radio"]["generated_sample_rate_hz"], 7850)
            self.assertIn("mono unsigned PCM8", manifest["objective_capabilities"]["modern_wav_radio"]["generated_postprocess"])
            self.assertIn("Ли Си Цин", manifest["campaign_sounds"][0]["text_ru"])
            self.assertTrue((output / "SVN" / "sounds" / "voice_cue_000_sample0.wav").exists())
            self.assertTrue((output / "SVN" / "sounds" / "voice_cue_000_sample0.txt").exists())
            self.assertEqual(generated["campaign"]["id"], "SVN")
            self.assertEqual(generated["campaign"]["schema"], "F15SE2_MODERN_CAMPAIGN_WLD")
            self.assertEqual(generated["campaign"]["schema_version"], 1)
            self.assertEqual(generated["campaign"]["base_theater"], "VN")
            self.assertEqual(generated["campaign"]["protagonist"]["name"], "Lisicin")
            self.assertEqual(generated["campaign"]["protagonist"]["callsign"], "Li Si Cin")
            self.assertEqual(generated["runtime_selection"]["argv"], ["--scenario", "SVN", "--scenario-base", "VN"])
            self.assertIn("stolen an F-15 plane", generated["scenario_design"]["premise"])
            self.assertEqual(generated["campaign_route_plan"]["format"], "F15SE2_CAMPAIGN_ROUTE_PLAN")
            self.assertEqual(len(generated["campaign_route_plan"]["waypoints"]), 6)
            self.assertEqual(generated["campaign_route_plan"]["waypoints"][4]["objective_id"], "strike_carrier_group")
            self.assertEqual(generated["campaign_route_plan"]["waypoints"][4]["x_coord"], 28300)
            self.assertEqual(generated["campaign_route_plan"]["waypoints"][4]["y_coord"], 10979)
            self.assertEqual(manifest["route_plan"]["source"], "SVN.WLD.json#campaign_route_plan")
            self.assertEqual(len(manifest["route_plan"]["waypoints"]), 6)
            self.assertEqual(generated["name_strings"][6], "USA CARRIER GROUP")
            self.assertEqual(generated["mission_plan"]["objectives"][1]["id"], "strike_carrier_group")
            self.assertEqual(generated["mission_plan"]["objectives"][1]["faction"], "usa")
            self.assertEqual(generated["mission_plan"]["objectives"][1]["domain"], "naval")
            self.assertEqual(generated["mission_plan"]["objectives"][1]["action"], "strike")
            self.assertEqual(generated["mission_plan"]["objectives"][1]["target_type"], "carrier_task_force")
            self.assertEqual(generated["mission_plan"]["objectives"][1]["threat_level"], "very_high")
            self.assertIn("disable carrier", generated["mission_plan"]["objectives"][1]["desired_effect"])
            self.assertTrue(generated["mission_plan"]["objectives"][1]["success_criteria"])
            self.assertIn("voice_cue_002_sample2_variant0", generated["mission_plan"]["objectives"][1]["radio_cue_ids"])
            self.assertEqual(generated["mission_plan"]["sortie_sequence"][1]["id"], "carrier_strike")
            self.assertEqual(generated["mission_plan"]["sortie_sequence"][1]["phase"], 2)
            self.assertEqual(generated["mission_plan"]["sortie_sequence"][1]["primary_objective_ids"], ["strike_carrier_group"])
            self.assertEqual(generated["mission_plan"]["sortie_sequence"][1]["secondary_objective_ids"], ["sweep_tonkin_patrol"])
            self.assertIn("captured F-15", generated["mission_plan"]["sortie_sequence"][1]["player_aircraft"])
            self.assertEqual(manifest["sortie_sequence"][1]["objective_ids"], ["strike_carrier_group", "sweep_tonkin_patrol"])
            self.assertEqual(manifest["briefings"][1]["sortie_id"], "carrier_strike")
            self.assertTrue((output / "SVN" / manifest["briefings"][1]["file"]).exists())
            self.assertEqual(generated["mission_plan"]["objectives"][1]["object_slot"], 6)
            self.assertEqual(generated["world_objects"][6]["scenario_role"], "primary_us_naval_target")
            self.assertEqual(generated["world_objects"][6]["scenario_objective_id"], "strike_carrier_group")
            self.assertEqual(generated["world_objects"][6]["scenario_domain"], "naval")
            self.assertEqual(generated["world_objects"][6]["x_coord"], 28300)
            self.assertEqual(generated["world_objects"][6]["y_coord"], 10979)
            self.assertEqual(generated["world_objects"][6]["scenario_real_world_anchor"]["label"], "Gulf of Tonkin carrier box")
            self.assertGreaterEqual(len(generated["target_annotations"]), 6)
            carrier_annotation = next(
                item
                for item in generated["target_annotations"]
                if item.get("scenario_objective_id") == "strike_carrier_group"
            )
            self.assertEqual(carrier_annotation["scenario_action"], "strike")
            self.assertEqual(carrier_annotation["scenario_target_type"], "carrier_task_force")
            self.assertEqual(carrier_annotation["scenario_threat_level"], "very_high")
            self.assertIn("disable carrier", carrier_annotation["scenario_desired_effect"])
            self.assertIn("voice_cue_002_sample2_variant0", carrier_annotation["radio_cue_ids"])
            self.assertEqual(carrier_annotation["coordinates"]["x_coord"], 28300)
            self.assertEqual(carrier_annotation["real_world_anchor"]["label"], "Gulf of Tonkin carrier box")
            self.assertGreater(len(build_wld(generated)), 0)

    def test_cli_validate_replacements_checks_generated_campaign_manifest(self):
        payload = {
            "format": "WLD",
            "terrain_target_ids": {"land": 0, "water": 0},
            "read_item_size": 0,
            "ground_unit_count": 0,
            "world_object_count": 0,
            "world_objects": [],
            "flight_unit_count": 0,
            "flight_units": [],
            "shape_target_category_table": base64.b64encode(bytes(100)).decode("ascii"),
            "kill_tally_or_unit_flags": base64.b64encode(bytes(100)).decode("ascii"),
            "mission_object_type_table": base64.b64encode(bytes(100)).decode("ascii"),
            "terrain_grid": base64.b64encode(bytes(256)).decode("ascii"),
            "name_table": base64.b64encode(b"").decode("ascii"),
            "name_strings": [],
            "trailing_bytes": "",
        }
        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            template = base / "VN.WLD.json"
            output = base / "converted_assets_all"
            missing_source = base / "missing_original_assets"
            template.write_text(json.dumps(payload), encoding="utf-8")

            self.assertEqual(cli_module.main(["new-campaign", str(template), str(output)]), 0)
            self.assertEqual(
                cli_module.main(["validate-replacements", str(missing_source), str(output), "--loadability-only"]),
                0,
            )
            launcher_path = output / "SVN" / "run_campaign.sh"
            good_launcher = launcher_path.read_text(encoding="utf-8")
            launcher_path.write_text("#!/bin/sh\necho broken\n", encoding="utf-8")
            self.assertEqual(cli_module.main(["refresh-campaign-inventory", str(output / "SVN")]), 0)
            self.assertEqual(
                cli_module.main(["validate-replacements", str(missing_source), str(output), "--loadability-only"]),
                1,
            )
            launcher_path.write_text(good_launcher, encoding="utf-8")
            launcher_path.chmod(launcher_path.stat().st_mode | 0o755)
            self.assertEqual(cli_module.main(["refresh-campaign-inventory", str(output / "SVN")]), 0)
            manifest_path = output / "SVN" / "campaign.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            del manifest["mission_objectives"][1]["target_type"]
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            self.assertEqual(
                cli_module.main(["validate-replacements", str(missing_source), str(output), "--loadability-only"]),
                1,
            )
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["mission_objectives"][1]["target_type"] = "carrier_task_force"
            manifest["verification_status"]["runtime_launch_validation"] = "passed"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            self.assertEqual(
                cli_module.main(["validate-replacements", str(missing_source), str(output), "--loadability-only"]),
                1,
            )
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["mission_objectives"][1]["target_type"] = "carrier_task_force"
            manifest["verification_status"]["runtime_launch_validation"] = "not_recorded_by_generator"
            manifest["verification_status"]["package_validation"] = "passed"
            manifest["verification_status"].pop("package_validation_evidence", None)
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            self.assertEqual(
                cli_module.main(["validate-replacements", str(missing_source), str(output), "--loadability-only"]),
                1,
            )

    def test_cli_list_campaigns_discovers_generated_manifest(self):
        payload = {
            "format": "WLD",
            "terrain_target_ids": {"land": 0, "water": 0},
            "read_item_size": 0,
            "ground_unit_count": 0,
            "world_object_count": 0,
            "world_objects": [],
            "flight_unit_count": 0,
            "flight_units": [],
            "shape_target_category_table": base64.b64encode(bytes(100)).decode("ascii"),
            "kill_tally_or_unit_flags": base64.b64encode(bytes(100)).decode("ascii"),
            "mission_object_type_table": base64.b64encode(bytes(100)).decode("ascii"),
            "terrain_grid": base64.b64encode(bytes(256)).decode("ascii"),
            "name_table": base64.b64encode(b"").decode("ascii"),
            "name_strings": [],
            "trailing_bytes": "",
        }
        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            template = base / "VN.WLD.json"
            output = base / "converted_assets_all"
            listing = base / "campaigns.json"
            template.write_text(json.dumps(payload), encoding="utf-8")

            self.assertEqual(cli_module.main(["new-campaign", str(template), str(output)]), 0)
            self.assertEqual(cli_module.main(["list-campaigns", str(output), "--json", str(listing)]), 0)
            campaigns = json.loads(listing.read_text(encoding="utf-8"))["campaigns"]
            self.assertEqual(campaigns[0]["id"], "SVN")
            self.assertEqual(campaigns[0]["launch"]["argv"], ["--campaign", "SVN"])
            self.assertTrue(campaigns[0]["manifest_path"].endswith("SVN/campaign.json"))
            self.assertEqual(
                campaigns[0]["verification_summary"]["runtime_launch_validation"],
                "not_recorded_by_generator",
            )
            self.assertEqual(
                campaigns[0]["verification_summary"]["replacement_loadability_validation"],
                "not_recorded_by_generator",
            )
            self.assertEqual(
                campaigns[0]["verification_summary"]["full_asset_replacement_validation"],
                "not_recorded_by_generator",
            )
            text_listing = io.StringIO()
            with contextlib.redirect_stdout(text_listing):
                self.assertEqual(cli_module.main(["list-campaigns", str(output)]), 0)
            listing_lines = text_listing.getvalue().splitlines()
            self.assertIn("runtime_launch_validation", listing_lines[0])
            self.assertIn("replacement_loadability_validation", listing_lines[0])
            self.assertIn("full_asset_replacement_validation", listing_lines[0])
            self.assertIn("not_recorded_by_generator", listing_lines[1])

    def test_cli_refresh_campaign_inventory_updates_hashes(self):
        payload = {
            "format": "WLD",
            "terrain_target_ids": {"land": 0, "water": 0},
            "read_item_size": 0,
            "ground_unit_count": 0,
            "world_object_count": 0,
            "world_objects": [],
            "flight_unit_count": 0,
            "flight_units": [],
            "shape_target_category_table": base64.b64encode(bytes(100)).decode("ascii"),
            "kill_tally_or_unit_flags": base64.b64encode(bytes(100)).decode("ascii"),
            "mission_object_type_table": base64.b64encode(bytes(100)).decode("ascii"),
            "terrain_grid": base64.b64encode(bytes(256)).decode("ascii"),
            "name_table": base64.b64encode(b"").decode("ascii"),
            "name_strings": [],
            "trailing_bytes": "",
        }
        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            template = base / "VN.WLD.json"
            output = base / "converted_assets_all"
            template.write_text(json.dumps(payload), encoding="utf-8")

            self.assertEqual(cli_module.main(["new-campaign", str(template), str(output)]), 0)
            inventory_path = output / "SVN" / "inventory.json"
            inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
            briefing_item = next(item for item in inventory["files"] if item["role"] == "sortie_briefing")
            old_hash = briefing_item["sha256"]
            (output / "SVN" / briefing_item["file"]).write_text("changed briefing\n", encoding="utf-8")

            self.assertEqual(cli_module.main(["refresh-campaign-inventory", str(output / "SVN")]), 0)
            refreshed = json.loads(inventory_path.read_text(encoding="utf-8"))
            refreshed_item = next(item for item in refreshed["files"] if item["file"] == briefing_item["file"])
            self.assertNotEqual(refreshed_item["sha256"], old_hash)

    def test_cli_package_campaign_uses_inventory(self):
        payload = {
            "format": "WLD",
            "terrain_target_ids": {"land": 0, "water": 0},
            "read_item_size": 0,
            "ground_unit_count": 0,
            "world_object_count": 0,
            "world_objects": [],
            "flight_unit_count": 0,
            "flight_units": [],
            "shape_target_category_table": base64.b64encode(bytes(100)).decode("ascii"),
            "kill_tally_or_unit_flags": base64.b64encode(bytes(100)).decode("ascii"),
            "mission_object_type_table": base64.b64encode(bytes(100)).decode("ascii"),
            "terrain_grid": base64.b64encode(bytes(256)).decode("ascii"),
            "name_table": base64.b64encode(b"").decode("ascii"),
            "name_strings": [],
            "trailing_bytes": "",
        }
        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            template = base / "VN.WLD.json"
            output = base / "converted_assets_all"
            archive_path = base / "SVN.zip"
            template.write_text(json.dumps(payload), encoding="utf-8")

            self.assertEqual(cli_module.main(["new-campaign", str(template), str(output)]), 0)
            self.assertEqual(cli_module.main(["package-campaign", str(output / "SVN"), str(archive_path)]), 0)
            with zipfile.ZipFile(archive_path, "r") as archive:
                names = set(archive.namelist())
                packaged_manifest = json.loads(archive.read("SVN/campaign.json").decode("utf-8"))
            self.assertIn("SVN/campaign.json", names)
            self.assertIn("SVN/SVN.WLD.json", names)
            self.assertIn("SVN/inventory.json", names)
            self.assertIn("SVN/package.json", names)
            self.assertIn("SVN/run_campaign.sh", names)
            self.assertEqual(packaged_manifest["verification_status"]["package_validation"], "passed")
            self.assertEqual(packaged_manifest["verification_status"]["package_validation_evidence"]["status"], "passed")
            self.assertTrue(packaged_manifest["verification_status"]["package_validation_evidence"]["recorded_at"])
            self.assertIn("package-campaign", packaged_manifest["verification_status"]["package_validation_evidence"]["command"])

    def test_cli_package_campaign_includes_campaign_local_aircraft_assets(self):
        payload = {
            "format": "WLD",
            "terrain_target_ids": {"land": 0, "water": 0},
            "read_item_size": 0,
            "ground_unit_count": 0,
            "world_object_count": 0,
            "world_objects": [],
            "flight_unit_count": 0,
            "flight_units": [],
            "shape_target_category_table": base64.b64encode(bytes(100)).decode("ascii"),
            "kill_tally_or_unit_flags": base64.b64encode(bytes(100)).decode("ascii"),
            "mission_object_type_table": base64.b64encode(bytes(100)).decode("ascii"),
            "terrain_grid": base64.b64encode(bytes(256)).decode("ascii"),
            "name_table": base64.b64encode(b"").decode("ascii"),
            "name_strings": [],
            "trailing_bytes": "",
        }
        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            template = base / "VN.WLD.json"
            output = base / "converted_assets_all"
            glb = base / "mig21.glb"
            archive_path = base / "SVN.zip"
            template.write_text(json.dumps(payload), encoding="utf-8")
            cli_module._write_svn_placeholder_glb(glb, 10, "mig21")

            self.assertEqual(cli_module.main(["new-campaign", str(template), str(output), "--aircraft-glb", f"10={glb}"]), 0)
            self.assertEqual(cli_module.main(["package-campaign", str(output / "SVN"), str(archive_path)]), 0)
            inspection_json = base / "inspection.json"
            self.assertEqual(cli_module.main(["inspect-campaign-package", str(archive_path), "--json", str(inspection_json)]), 0)
            inspection = json.loads(inspection_json.read_text(encoding="utf-8"))
            self.assertEqual(
                inspection["verification_summary"]["runtime_launch_validation"],
                "not_recorded_by_generator",
            )
            self.assertEqual(
                inspection["verification_summary"]["replacement_loadability_validation"],
                "not_recorded_by_generator",
            )
            self.assertEqual(
                inspection["verification_summary"]["full_asset_replacement_validation"],
                "not_recorded_by_generator",
            )
            with zipfile.ZipFile(archive_path, "r") as archive:
                names = set(archive.namelist())
                package_manifest = json.loads(archive.read("SVN/package.json").decode("utf-8"))
            self.assertIn("SVN/15FLT/shape_010_mig21.glb", names)
            self.assertFalse(package_manifest["includes_external_assets"])
            installed = base / "installed_assets"
            self.assertEqual(cli_module.main(["install-campaign-package", str(archive_path), str(installed)]), 0)
            self.assertEqual((installed / "SVN" / "15FLT" / "shape_010_mig21.glb").read_bytes(), glb.read_bytes())

    def test_cli_install_campaign_package_installs_discoverable_campaign(self):
        payload = {
            "format": "WLD",
            "terrain_target_ids": {"land": 0, "water": 0},
            "read_item_size": 0,
            "ground_unit_count": 0,
            "world_object_count": 0,
            "world_objects": [],
            "flight_unit_count": 0,
            "flight_units": [],
            "shape_target_category_table": base64.b64encode(bytes(100)).decode("ascii"),
            "kill_tally_or_unit_flags": base64.b64encode(bytes(100)).decode("ascii"),
            "mission_object_type_table": base64.b64encode(bytes(100)).decode("ascii"),
            "terrain_grid": base64.b64encode(bytes(256)).decode("ascii"),
            "name_table": base64.b64encode(b"").decode("ascii"),
            "name_strings": [],
            "trailing_bytes": "",
        }
        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            template = base / "VN.WLD.json"
            output = base / "converted_assets_all"
            installed = base / "installed_assets"
            archive_path = base / "SVN.zip"
            listing = base / "installed_campaigns.json"
            template.write_text(json.dumps(payload), encoding="utf-8")

            self.assertEqual(cli_module.main(["new-campaign", str(template), str(output)]), 0)
            self.assertEqual(cli_module.main(["package-campaign", str(output / "SVN"), str(archive_path)]), 0)
            self.assertEqual(cli_module.main(["install-campaign-package", str(archive_path), str(installed), "--dry-run"]), 0)
            self.assertFalse((installed / "SVN").exists())
            self.assertEqual(cli_module.main(["install-campaign-package", str(archive_path), str(installed)]), 0)
            self.assertEqual(cli_module.main(["list-campaigns", str(installed), "--json", str(listing)]), 0)
            campaigns = json.loads(listing.read_text(encoding="utf-8"))["campaigns"]
            self.assertEqual(campaigns[0]["id"], "SVN")

    def test_cli_inspect_campaign_package_outputs_metadata(self):
        payload = {
            "format": "WLD",
            "terrain_target_ids": {"land": 0, "water": 0},
            "read_item_size": 0,
            "ground_unit_count": 0,
            "world_object_count": 0,
            "world_objects": [],
            "flight_unit_count": 0,
            "flight_units": [],
            "shape_target_category_table": base64.b64encode(bytes(100)).decode("ascii"),
            "kill_tally_or_unit_flags": base64.b64encode(bytes(100)).decode("ascii"),
            "mission_object_type_table": base64.b64encode(bytes(100)).decode("ascii"),
            "terrain_grid": base64.b64encode(bytes(256)).decode("ascii"),
            "name_table": base64.b64encode(b"").decode("ascii"),
            "name_strings": [],
            "trailing_bytes": "",
        }
        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            template = base / "VN.WLD.json"
            output = base / "converted_assets_all"
            archive_path = base / "SVN.zip"
            info_path = base / "package_info.json"
            template.write_text(json.dumps(payload), encoding="utf-8")

            self.assertEqual(cli_module.main(["new-campaign", str(template), str(output)]), 0)
            self.assertEqual(cli_module.main(["package-campaign", str(output / "SVN"), str(archive_path)]), 0)
            self.assertEqual(cli_module.main(["verify-campaign-package", str(archive_path)]), 0)
            self.assertEqual(cli_module.main(["inspect-campaign-package", str(archive_path), "--json", str(info_path)]), 0)
            info = json.loads(info_path.read_text(encoding="utf-8"))
            self.assertEqual(info["campaign"]["id"], "SVN")
            self.assertEqual(info["package"]["format"], "F15SE2_CAMPAIGN_PACKAGE")
            self.assertEqual(info["route_summary"]["waypoints"], 6)
            self.assertEqual(info["route_summary"]["sortie_routes"], 3)
            self.assertEqual(info["target_summary"]["mission_target_sets"], 3)
            self.assertIn("SVN/campaign.json", info["entries"])
            self.assertIn("SVN/run_campaign.sh", info["entries"])
            self.assertEqual(info["errors"], [])
            world_payload = json.loads((output / "SVN" / "SVN.WLD.json").read_text(encoding="utf-8"))
            world_payload["mission_plan"]["mission_target_sets"][1]["package_role"] = "edited in map editor"
            (output / "SVN" / "SVN.WLD.json").write_text(json.dumps(world_payload), encoding="utf-8")
            self.assertEqual(cli_module.main(["refresh-campaign-inventory", str(output / "SVN")]), 0)
            synced_manifest = json.loads((output / "SVN" / "campaign.json").read_text(encoding="utf-8"))
            self.assertEqual(synced_manifest["mission_target_sets"][1]["package_role"], "edited in map editor")
            briefing_path = output / "SVN" / synced_manifest["briefings"][0]["file"]
            briefing_path.write_text(
                "# Edited briefing title\n\nSortie id: `opening_sead`\nRadio cues: `voice_cue_000_sample0`\n\nEdited briefing summary from Markdown.\n",
                encoding="utf-8",
            )
            self.assertEqual(cli_module.main(["refresh-campaign-inventory", str(output / "SVN")]), 0)
            synced_manifest = json.loads((output / "SVN" / "campaign.json").read_text(encoding="utf-8"))
            self.assertEqual(synced_manifest["briefings"][0]["title"], "Edited briefing title")
            self.assertEqual(synced_manifest["briefings"][0]["summary"], "Edited briefing summary from Markdown.")
            self.assertEqual(synced_manifest["briefings"][0]["radio_cue_ids"], ["voice_cue_000_sample0"])
            stale_manifest = json.loads((output / "SVN" / "campaign.json").read_text(encoding="utf-8"))
            stale_manifest["mission_target_sets"][1]["package_role"] = "stale manifest value"
            stale_manifest["briefings"][0]["summary"] = "stale briefing summary"
            (output / "SVN" / "campaign.json").write_text(json.dumps(stale_manifest), encoding="utf-8")
            checked, failed = cli_module.validate_campaign_manifests(output)
            self.assertEqual(checked, 1)
            self.assertEqual(failed, 1)
            self.assertEqual(cli_module.main(["refresh-campaign-inventory", str(output / "SVN")]), 0)
            (output / "SVN" / "run_campaign.sh").write_text("#!/bin/sh\necho broken\n", encoding="utf-8")
            self.assertEqual(cli_module.main(["refresh-campaign-inventory", str(output / "SVN")]), 0)
            broken_archive = base / "SVN_broken.zip"
            self.assertEqual(cli_module.main(["package-campaign", str(output / "SVN"), str(broken_archive), "--skip-validation"]), 0)
            self.assertEqual(cli_module.main(["inspect-campaign-package", str(broken_archive)]), 1)

            world_payload = json.loads((output / "SVN" / "SVN.WLD.json").read_text(encoding="utf-8"))
            world_payload["campaign_route_plan"]["sortie_routes"][0]["route"] = ["wp_hq", "missing_waypoint"]
            (output / "SVN" / "SVN.WLD.json").write_text(json.dumps(world_payload), encoding="utf-8")
            self.assertEqual(cli_module.main(["refresh-campaign-inventory", str(output / "SVN")]), 0)
            synced_manifest = json.loads((output / "SVN" / "campaign.json").read_text(encoding="utf-8"))
            self.assertEqual(synced_manifest["route_plan"]["sortie_routes"][0]["route"], ["wp_hq", "missing_waypoint"])
            broken_route_archive = base / "SVN_broken_route.zip"
            self.assertEqual(cli_module.main(["package-campaign", str(output / "SVN"), str(broken_route_archive), "--skip-validation"]), 0)
            self.assertEqual(cli_module.main(["verify-campaign-package", str(broken_route_archive)]), 1)
            broken_install = base / "broken_install"
            self.assertEqual(cli_module.main(["install-campaign-package", str(broken_route_archive), str(broken_install)]), 1)
            self.assertFalse((broken_install / "SVN").exists())

    def test_cli_install_campaign_package_requires_replace_for_existing_campaign(self):
        payload = {
            "format": "WLD",
            "terrain_target_ids": {"land": 0, "water": 0},
            "read_item_size": 0,
            "ground_unit_count": 0,
            "world_object_count": 0,
            "world_objects": [],
            "flight_unit_count": 0,
            "flight_units": [],
            "shape_target_category_table": base64.b64encode(bytes(100)).decode("ascii"),
            "kill_tally_or_unit_flags": base64.b64encode(bytes(100)).decode("ascii"),
            "mission_object_type_table": base64.b64encode(bytes(100)).decode("ascii"),
            "terrain_grid": base64.b64encode(bytes(256)).decode("ascii"),
            "name_table": base64.b64encode(b"").decode("ascii"),
            "name_strings": [],
            "trailing_bytes": "",
        }
        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            template = base / "VN.WLD.json"
            output = base / "converted_assets_all"
            installed = base / "installed_assets"
            archive_path = base / "SVN.zip"
            template.write_text(json.dumps(payload), encoding="utf-8")

            self.assertEqual(cli_module.main(["new-campaign", str(template), str(output)]), 0)
            self.assertEqual(cli_module.main(["package-campaign", str(output / "SVN"), str(archive_path)]), 0)
            self.assertEqual(cli_module.main(["install-campaign-package", str(archive_path), str(installed)]), 0)
            launcher = installed / "SVN" / "run_campaign.sh"
            self.assertTrue(launcher.exists())
            self.assertTrue(launcher.stat().st_mode & 0o111)
            self.assertEqual(cli_module.main(["install-campaign-package", str(archive_path), str(installed)]), 1)
            self.assertEqual(cli_module.main(["install-campaign-package", str(archive_path), str(installed), "--replace"]), 0)
            self.assertTrue(launcher.stat().st_mode & 0o111)

    def test_cli_install_aircraft_glb_replaces_slot_and_clears_cache(self):
        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            source = base / "custom.glb"
            source.write_bytes(b"custom aircraft glb")
            converted = base / "converted_assets_all"
            aircraft_dir = converted / "15FLT"
            cache_dir = aircraft_dir / "cache"
            cache_dir.mkdir(parents=True)
            existing = aircraft_dir / "shape_010_F15.glb"
            existing.write_bytes(b"old")
            stale_root_cache = aircraft_dir / "shape_010_F15.glmesh"
            stale_cache = cache_dir / "shape_010_F15.glmesh"
            stale_root_cache.write_bytes(b"stale root")
            stale_cache.write_bytes(b"stale cache")

            rc = cli_module.main(["install-aircraft-glb", str(source), str(base), "--slot", "10", "--skip-validate"])
            self.assertEqual(rc, 0)
            self.assertEqual(existing.read_bytes(), b"custom aircraft glb")
            self.assertFalse(stale_root_cache.exists())
            self.assertFalse(stale_cache.exists())

    def test_cli_new_campaign_can_install_font_overrides(self):
        payload = {
            "format": "WLD",
            "terrain_target_ids": {"land": 0, "water": 0},
            "read_item_size": 0,
            "ground_unit_count": 0,
            "world_object_count": 0,
            "world_objects": [],
            "flight_unit_count": 0,
            "flight_units": [],
            "shape_target_category_table": base64.b64encode(bytes(100)).decode("ascii"),
            "kill_tally_or_unit_flags": base64.b64encode(bytes(100)).decode("ascii"),
            "mission_object_type_table": base64.b64encode(bytes(100)).decode("ascii"),
            "terrain_grid": base64.b64encode(bytes(256)).decode("ascii"),
            "name_table": base64.b64encode(b"").decode("ascii"),
            "name_strings": [],
            "trailing_bytes": "",
        }
        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            template = base / "VN.WLD.json"
            output = base / "converted_assets_all"
            font = base / "custom.ttf"
            template.write_text(json.dumps(payload), encoding="utf-8")
            font.write_bytes(b"fake font bytes")

            rc = cli_module.main(["new-campaign", str(template), str(output), "--font", str(font)])
            self.assertEqual(rc, 0)
            manifest = json.loads((output / "SVN" / "campaign.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["installed_fonts"], ["fonts/font_1.ttf", "fonts/font_3.ttf", "fonts/font_4.ttf"])
            self.assertTrue(manifest["objective_capabilities"]["modern_fonts"]["provided"])
            self.assertEqual(manifest["objective_capabilities"]["modern_fonts"]["artifacts"], manifest["installed_fonts"])
            self.assertEqual((output / "SVN" / "fonts" / "font_1.ttf").read_bytes(), b"fake font bytes")
            self.assertEqual((output / "SVN" / "fonts" / "font_3.ttf").read_bytes(), b"fake font bytes")
            self.assertEqual((output / "SVN" / "fonts" / "font_4.ttf").read_bytes(), b"fake font bytes")

    def test_cli_new_campaign_can_install_aircraft_glb(self):
        payload = {
            "format": "WLD",
            "terrain_target_ids": {"land": 0, "water": 0},
            "read_item_size": 0,
            "ground_unit_count": 0,
            "world_object_count": 0,
            "world_objects": [],
            "flight_unit_count": 0,
            "flight_units": [],
            "shape_target_category_table": base64.b64encode(bytes(100)).decode("ascii"),
            "kill_tally_or_unit_flags": base64.b64encode(bytes(100)).decode("ascii"),
            "mission_object_type_table": base64.b64encode(bytes(100)).decode("ascii"),
            "terrain_grid": base64.b64encode(bytes(256)).decode("ascii"),
            "name_table": base64.b64encode(b"").decode("ascii"),
            "name_strings": [],
            "trailing_bytes": "",
        }
        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            template = base / "VN.WLD.json"
            output = base / "converted_assets_all"
            glb = base / "mig21.glb"
            template.write_text(json.dumps(payload), encoding="utf-8")
            cli_module._write_svn_placeholder_glb(glb, 10, "mig21")

            rc = cli_module.main(["new-campaign", str(template), str(output), "--aircraft-glb", f"10={glb}"])
            self.assertEqual(rc, 0)
            manifest = json.loads((output / "SVN" / "campaign.json").read_text(encoding="utf-8"))
            readme = (output / "SVN" / "README.md").read_text(encoding="utf-8")
            self.assertEqual(len(manifest["installed_aircraft"]), 23)
            slot_10 = next(item for item in manifest["installed_aircraft"] if item["slot"] == 10)
            self.assertEqual(slot_10["file"], "15FLT/shape_010_mig21.glb")
            self.assertEqual(slot_10["license_status"], "user_supplied_review_required")
            self.assertEqual((output / "SVN" / "15FLT" / "shape_010_mig21.glb").read_bytes(), glb.read_bytes())
            self.assertIn("shape_010_mig21.glb", readme)

    def test_cli_new_campaign_can_install_recorded_radio_cues(self):
        payload = {
            "format": "WLD",
            "terrain_target_ids": {"land": 0, "water": 0},
            "read_item_size": 0,
            "ground_unit_count": 0,
            "world_object_count": 0,
            "world_objects": [],
            "flight_unit_count": 0,
            "flight_units": [],
            "shape_target_category_table": base64.b64encode(bytes(100)).decode("ascii"),
            "kill_tally_or_unit_flags": base64.b64encode(bytes(100)).decode("ascii"),
            "mission_object_type_table": base64.b64encode(bytes(100)).decode("ascii"),
            "terrain_grid": base64.b64encode(bytes(256)).decode("ascii"),
            "name_table": base64.b64encode(b"").decode("ascii"),
            "name_strings": [],
            "trailing_bytes": "",
        }
        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            template = base / "VN.WLD.json"
            output = base / "converted_assets_all"
            radio_dir = base / "radio"
            radio_dir.mkdir()
            cue = radio_dir / "voice_cue_000_sample0.wav"
            template.write_text(json.dumps(payload), encoding="utf-8")
            cue.write_bytes(b"recorded russian voice")

            rc = cli_module.main(["new-campaign", str(template), str(output), "--radio-dir", str(radio_dir)])
            self.assertEqual(rc, 0)
            manifest = json.loads((output / "SVN" / "campaign.json").read_text(encoding="utf-8"))
            self.assertEqual((output / "SVN" / "sounds" / "voice_cue_000_sample0.wav").read_bytes(), b"recorded russian voice")
            self.assertEqual(manifest["campaign_sounds"][0]["kind"], "installed_recorded_or_tts_wav")
            self.assertEqual(manifest["campaign_sounds"][0]["source"], str(cue))

    def test_cli_new_campaign_can_generate_radio_cues_with_tts_command(self):
        payload = {
            "format": "WLD",
            "terrain_target_ids": {"land": 0, "water": 0},
            "read_item_size": 0,
            "ground_unit_count": 0,
            "world_object_count": 0,
            "world_objects": [],
            "flight_unit_count": 0,
            "flight_units": [],
            "shape_target_category_table": base64.b64encode(bytes(100)).decode("ascii"),
            "kill_tally_or_unit_flags": base64.b64encode(bytes(100)).decode("ascii"),
            "mission_object_type_table": base64.b64encode(bytes(100)).decode("ascii"),
            "terrain_grid": base64.b64encode(bytes(256)).decode("ascii"),
            "name_table": base64.b64encode(b"").decode("ascii"),
            "name_strings": [],
            "trailing_bytes": "",
        }
        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            template = base / "VN.WLD.json"
            output = base / "converted_assets_all"
            template.write_text(json.dumps(payload), encoding="utf-8")

            command = "/bin/sh -c 'printf tts > \"{output}\"'"
            rc = cli_module.main(["new-campaign", str(template), str(output), "--radio-tts-command", command])
            self.assertEqual(rc, 0)
            manifest = json.loads((output / "SVN" / "campaign.json").read_text(encoding="utf-8"))
            self.assertEqual((output / "SVN" / "sounds" / "voice_cue_000_sample0.wav").read_bytes(), b"tts")
            self.assertEqual(manifest["campaign_sounds"][0]["kind"], "generated_tts_wav")
            self.assertEqual(manifest["campaign_sounds"][0]["postprocess"], "left as generated by TTS command")
            self.assertIsNone(manifest["campaign_sounds"][0]["sample_rate_hz"])
            self.assertEqual(manifest["campaign_sounds"][0]["tts_command_template"], command)

    def test_cli_radio_tts_postprocesses_valid_wav_to_game_pcm8(self):
        with tempfile.TemporaryDirectory() as workdir:
            path = pathlib.Path(workdir) / "cue.wav"
            with wave.open(str(path), "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(15700)
                wav.writeframes(struct.pack("<hhhhhh", -12000, -6000, 0, 6000, 12000, 0))

            self.assertTrue(cli_module._rewrite_tts_wav_as_radio_pcm8(path, 2))
            with wave.open(str(path), "rb") as wav:
                self.assertEqual(wav.getnchannels(), 1)
                self.assertEqual(wav.getsampwidth(), 1)
                self.assertEqual(wav.getframerate(), 7850)
                self.assertGreater(wav.getnframes(), 0)

    def test_cli_install_aircraft_glb_creates_named_slot_when_missing(self):
        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            source = base / "mig21.glb"
            source.write_bytes(b"mig")
            converted = base / "converted_assets_all"

            rc = cli_module.main([
                "install-aircraft-glb",
                str(source),
                str(converted),
                "--slot",
                "21",
                "--label",
                "MiG-21 Fishbed",
                "--skip-validate",
            ])
            self.assertEqual(rc, 0)
            self.assertEqual((converted / "15FLT" / "shape_021_MiG-21_Fishbed.glb").read_bytes(), b"mig")

    def test_3d3_to_gltf_export(self):
        render_mode = bytes([0x00])
        face_info = bytes([0x04] + [0x00] * 32)
        vertices = bytes(
            [0x03,  # vertex header: count=3, non-shared
             0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  # v0 mask + (0,0,0)
             0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,  # v1 mask + (1,0,0)
             0xFF, 0xFF, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00]  # v2 mask + (0,1,0)
        )
        edges = bytes([0x03, 0xFF, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x01, 0x02, 0xFF, 0xFF, 0x02, 0x00])
        primitive = bytes([0x01, 0x05, 0x03, 0x00, 0x01, 0x02, 0x01])  # one fill polygon
        shape_data = render_mode + face_info + vertices + edges + primitive

        blob = bytearray()
        blob.extend((0x33, 0x33))  # signature
        blob.extend((1, 0))  # shape count
        blob.extend((0, 0))  # shape offset[0]
        blob.extend((len(shape_data) & 0xFF, (len(shape_data) >> 8) & 0xFF))
        blob.extend(shape_data)

        payload = parse_3d3(bytes(blob))
        gltf = export_3d3_to_gltf(payload)
        self.assertEqual(gltf["asset"]["version"], "2.0")
        self.assertEqual(len(gltf["meshes"]), 1)
        self.assertEqual(len(gltf["nodes"]), 1)
        first_mesh = gltf["meshes"][0]
        self.assertEqual(gltf["extras"]["format"], "3D3")
        self.assertFalse(gltf["extras"]["has_shared_vertex_pool"])
        self.assertTrue(first_mesh["primitives"])
        self.assertTrue(any(primitive["mode"] == 4 for primitive in first_mesh["primitives"]))

    def test_3d3_gltf_includes_shared_pool_metadata(self):
        render_mode = bytes([0x00])
        face_info = bytes([0x00] + [0x00] * 32)
        vertices = bytes(
            [
                0x81,       # vertex header: shared-vertex entry + 1 vertex
                0xFF, 0xFF, # visibility mask
                0x00,       # vertex index 0 -> shared arrays
            ]
        )
        edges = bytes([0x00])  # no edges
        primitive_count = bytes([0x00])
        shape_data = render_mode + face_info + vertices + edges + primitive_count

        payload = {
            "format": "3D3",
            "shape_offsets": [0],
            "model_data": base64.b64encode(shape_data).decode("ascii"),
            "model_data_size": len(shape_data),
            "shared_vertex_pool": {
                "x_indices": [0],
                "y_indices": [0],
                "z_indices": [0],
                "x_values": [7],
                "y_values": [8],
                "z_values": [9],
            },
            "trailing_bytes": "",
        }

        gltf = export_3d3_to_gltf(payload)
        self.assertTrue(gltf["extras"]["has_shared_vertex_pool"])
        self.assertEqual(gltf["extras"]["shared_vertex_pool"]["index_count"], 1)
        self.assertEqual(gltf["extras"]["skipped_shapes"][0]["render_mode"], 0)

    def test_3d3_to_glb_export(self):
        render_mode = bytes([0x00])
        face_info = bytes([0x04] + [0x00] * 32)
        vertices = bytes(
            [0x03,  # vertex header: count=3, non-shared
             0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  # v0 mask + (0,0,0)
             0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,  # v1 mask + (1,0,0)
             0xFF, 0xFF, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00]  # v2 mask + (0,1,0)
        )
        edges = bytes([0x03, 0xFF, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x01, 0x02, 0xFF, 0xFF, 0x02, 0x00])
        primitive = bytes([0x01, 0x05, 0x03, 0x00, 0x01, 0x02, 0x01])  # one fill polygon
        shape_data = render_mode + face_info + vertices + edges + primitive

        blob = bytearray()
        blob.extend((0x33, 0x33))  # signature
        blob.extend((1, 0))  # shape count
        blob.extend((0, 0))  # shape offset[0]
        blob.extend((len(shape_data) & 0xFF, (len(shape_data) >> 8) & 0xFF))
        blob.extend(shape_data)

        payload = parse_3d3(bytes(blob))
        glb = export_3d3_to_glb(payload)

        self.assertGreaterEqual(len(glb), 20)
        self.assertEqual(glb[:4], b"glTF")
        self.assertEqual(struct.unpack("<I", glb[4:8])[0], 2)
        total_length = struct.unpack("<I", glb[8:12])[0]
        self.assertEqual(total_length, len(glb))

        json_chunk_length = struct.unpack("<I", glb[12:16])[0]
        self.assertGreater(json_chunk_length, 0)
        self.assertEqual(glb[16:20], b"JSON")
        self.assertGreaterEqual(len(glb), 20 + json_chunk_length)

    def test_3d3_shape_glmesh_cache_matches_glb_source(self):
        render_mode = bytes([0x00])
        face_info = bytes([0x04] + [0x00] * 32)
        vertices = bytes(
            [0x03,
             0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
             0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
             0xFF, 0xFF, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00]
        )
        edges = bytes([0x03, 0xFF, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x01, 0x02, 0xFF, 0xFF, 0x02, 0x00])
        primitive = bytes([0x01, 0x05, 0x03, 0x00, 0x01, 0x02, 0x01])
        shape_data = render_mode + face_info + vertices + edges + primitive

        blob = bytearray()
        blob.extend((0x33, 0x33))
        blob.extend((1, 0))
        blob.extend((0, 0))
        blob.extend((len(shape_data) & 0xFF, (len(shape_data) >> 8) & 0xFF))
        blob.extend(shape_data)

        payload = parse_3d3(bytes(blob))
        shapes = export_3d3_shape_gltfs(payload)
        self.assertEqual(len(shapes), 1)

        with tempfile.TemporaryDirectory() as workdir:
            glb_path = pathlib.Path(workdir) / "shape_000.glb"
            glmesh_path = pathlib.Path(workdir) / "shape_000.glmesh"
            glb_path.write_bytes(export_3d3_gltf_to_glb(shapes[0][2]))
            glmesh_path.write_bytes(cli_module._glb_to_glmesh_bytes(glb_path))
            glmesh = glmesh_path.read_bytes()
            self.assertEqual(glmesh, cli_module._glb_to_glmesh_bytes(glb_path))
            self.assertEqual(glmesh[:8], b"F15GLM3\x00")
            self.assertGreater(len(glmesh), 12)

    def test_3d3_to_gltf_tolerates_truncated_shape_payload(self):
        payload = {
            "format": "3D3",
            "shape_offsets": [0],
            "model_data": base64.b64encode(bytes([7, 7, 16, 61, 62, 63, 64, 34, 65])).decode("ascii"),
            "model_data_size": 9,
            "shared_vertex_pool": None,
            "trailing_bytes": "",
        }

        gltf = export_3d3_to_gltf(payload)
        self.assertEqual(len(gltf["meshes"]), 0)
        self.assertEqual(
            gltf["extras"]["skipped_shapes"][0]["shape_payload"]["render_error"],
            "truncated 3D3 face-normal block",
        )

    def test_cli_decode_writes_stable_sidecars(self):
        asset_root = pathlib.Path("/home/xor/games/f15")
        if not asset_root.exists():
            self.skipTest("missing asset tree")

        sample_pic = next(iter(sorted(asset_root.glob("*.PIC"))), None)
        if sample_pic is None:
            sample_pic = next(iter(sorted(asset_root.glob("*.pic"))), None)
        if sample_pic is None:
            sample_pic = next(iter(sorted(asset_root.glob("*.SPR"))), None)
        if sample_pic is None:
            sample_pic = next(iter(sorted(asset_root.glob("*.spr"))), None)

        sample_3d3 = next(iter(sorted(asset_root.glob("*.3D3"))), None)
        if sample_3d3 is None:
            self.skipTest("missing .3D3 sample")

        sample_3dt = next(iter(sorted(asset_root.glob("*.3DT"))), None)
        if sample_3dt is None:
            self.skipTest("missing .3DT sample")

        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)

            json_3d3 = base / "asset_3d3.json"
            gltf = base / "asset_3d3.gltf"

            if sample_pic is not None:
                json_pic = base / "asset_pic.json"
                png_pic = base / "asset_pic.png"
                rc = cli_module.main(
                    [
                        "decode",
                        str(sample_pic),
                        str(json_pic),
                        "--png",
                        str(png_pic),
                    ]
                )
                self.assertEqual(rc, 0)
                self.assertTrue(json_pic.exists())
                self.assertTrue(png_pic.exists())
                payload_pic = json.loads(json_pic.read_text(encoding="utf-8"))
                self.assertEqual(payload_pic["format"], "PIC")

            rc = cli_module.main(
                [
                    "decode",
                    str(sample_3d3),
                    str(json_3d3),
                    "--gltf",
                    str(gltf),
                ]
            )
            self.assertEqual(rc, 0)
            self.assertTrue(json_3d3.exists())
            self.assertTrue(gltf.exists())
            payload = json.loads(json_3d3.read_text(encoding="utf-8"))
            self.assertEqual(payload["format"], "3D3")

            json_3dt = base / "asset_3dt.json"
            rc = cli_module.main(
                [
                    "decode",
                    str(sample_3dt),
                    str(json_3dt),
                ]
            )
            self.assertEqual(rc, 0)
            self.assertTrue(json_3dt.exists())
            payload = json.loads(json_3dt.read_text(encoding="utf-8"))
            self.assertEqual(payload["format"], "3DT")

            sample_3dg = next(iter(sorted(asset_root.glob("*.3DG"))), None)
            if sample_3dg is None:
                sample_3dg = next(iter(sorted(asset_root.glob("*.3dg"))), None)
            if sample_3dg is None:
                self.skipTest("missing .3DG sample")

            json_3dg = base / "asset_3dg.json"
            rc = cli_module.main(
                [
                    "decode",
                    str(sample_3dg),
                    str(json_3dg),
                ]
            )
            self.assertEqual(rc, 0)
            self.assertTrue(json_3dg.exists())
            payload = json.loads(json_3dg.read_text(encoding="utf-8"))
            self.assertEqual(payload["format"], "3DG")

            sample_wld = next(iter(sorted(asset_root.glob("*.WLD"))), None)
            if sample_wld is None:
                sample_wld = next(iter(sorted(asset_root.glob("*.wld"))), None)
            if sample_wld is None:
                self.skipTest("missing .WLD sample")

            json_wld = base / "asset_wld.json"
            rc = cli_module.main(
                [
                    "decode",
                    str(sample_wld),
                    str(json_wld),
                ]
            )
            self.assertEqual(rc, 0)
            self.assertTrue(json_wld.exists())
            payload = json.loads(json_wld.read_text(encoding="utf-8"))
            self.assertEqual(payload["format"], "WLD")

    def test_cli_convert_tree_produces_modern_artifacts(self):
        with tempfile.TemporaryDirectory() as workdir:
            source_root = pathlib.Path(workdir) / "assets"
            output_root = pathlib.Path(workdir) / "out"
            nested = source_root / "nested"
            source_root.mkdir()
            nested.mkdir()

            pic_payload = {
                "format": "PIC",
                "decoded_width": 320,
                "decoded_height": 200,
                "max_lzw_width": 12,
                "bitstream_mode": "byte",
                "pixels_base64": base64.b64encode(bytes((i & 0xFF) for i in range(320 * 200))).decode("ascii"),
            }
            (source_root / "TITLE.PIC").write_bytes(encode_pic_asset(pic_payload))

            shape_data = bytes([0x00])  # render mode
            face_info = bytes([0x00] + [0x00] * 32)
            vertices = bytes(
                [
                    0x00,  # vertex header: vertex_count=0, non-shared
                ]
            )
            edges = bytes([0x00])  # no edges
            primitive = bytes([0x00])  # no primitives
            shape_data += face_info + vertices + edges + primitive

            shape_blob = bytearray()
            shape_blob.extend((0x33, 0x33))
            shape_blob.extend((1, 0))
            shape_blob.extend((0, 0))
            shape_blob.extend((len(shape_data) & 0xFF, (len(shape_data) >> 8) & 0xFF))
            shape_blob.extend(shape_data)
            (source_root / "SCENERY.3D3").write_bytes(bytes(shape_blob))

            tdt_payload = {
                "format": "3DT",
                "version": 1,
                "levels": [
                    {"level": 0, "objects": []},
                    {"level": 1, "objects": []},
                    {"level": 2, "objects": []},
                    {"level": 3, "objects": []},
                    {"level": 4, "objects": []},
                ],
            }
            (nested / "TACTICS.3DT").write_bytes(build_3dt(tdt_payload))

            grid_payload = {
                "format": "3DG",
                "version": 1,
                "level4_top_grid": [0] * 16,
                "level3_grid": [1] * 256,
                "level2_subgrid": [2] * 512,
                "level1_subgrid": [3] * 512,
                "level0_subgrid": [4] * 512,
            }
            (nested / "LANDS.3DG").write_bytes(build_3dg(grid_payload))

            wld_payload = {
                "format": "WLD",
                "terrain_target_ids": {"land": 0, "water": 0},
                "read_item_size": 0,
                "ground_unit_count": 0,
                "world_object_count": 0,
                "world_objects": [],
                "flight_unit_count": 0,
                "flight_units": [],
                "shape_target_category_table": base64.b64encode(bytes([1] * 100)).decode("ascii"),
                "kill_tally_or_unit_flags": base64.b64encode(bytes([2] * 100)).decode("ascii"),
                "mission_object_type_table": base64.b64encode(bytes([3] * 100)).decode("ascii"),
                "terrain_grid": base64.b64encode(bytes(range(256))).decode("ascii"),
                "name_table": base64.b64encode(b"NODE\x00VALUE\x00").decode("ascii"),
            }
            (source_root / "WORLD.WLD").write_bytes(build_wld(wld_payload))

            rc = cli_module.main(
                [
                    "convert-tree",
                    str(source_root),
                    str(output_root),
                    "--recursive",
                    "--models",
                    "glb",
                ]
            )
            self.assertEqual(rc, 0)

            self.assertFalse((output_root / "TITLE.json").exists())
            self.assertTrue((output_root / "TITLE.png").exists())

            self.assertTrue((output_root / "SCENERY" / "SCENERY.3D3.json").exists())
            self.assertTrue((output_root / "SCENERY" / "SCENERY.3D3.glb").exists())
            self.assertFalse((output_root / "SCENERY" / "shape_000.glb").exists())

            self.assertTrue((output_root / "nested" / "TACTICS" / "TACTICS.3DT.json").exists())

            self.assertTrue((output_root / "nested" / "LANDS" / "LANDS.3DG.json").exists())

            self.assertTrue((output_root / "WORLD" / "WORLD.WLD.json").exists())

    def test_cli_build_soviet_vietnam_pack_converts_and_scaffolds_campaign(self):
        with tempfile.TemporaryDirectory() as workdir:
            source_root = pathlib.Path(workdir) / "assets"
            output_root = pathlib.Path(workdir) / "converted_assets_all"
            source_root.mkdir()

            pic_payload = {
                "format": "PIC",
                "decoded_width": 320,
                "decoded_height": 200,
                "max_lzw_width": 12,
                "bitstream_mode": "byte",
                "pixels_base64": base64.b64encode(bytes(320 * 200)).decode("ascii"),
            }
            (source_root / "TITLE.PIC").write_bytes(encode_pic_asset(pic_payload))
            wld_payload = {
                "format": "WLD",
                "terrain_target_ids": {"land": 0, "water": 0},
                "read_item_size": 0,
                "ground_unit_count": 0,
                "world_object_count": 0,
                "world_objects": [],
                "flight_unit_count": 0,
                "flight_units": [],
                "shape_target_category_table": base64.b64encode(bytes(100)).decode("ascii"),
                "kill_tally_or_unit_flags": base64.b64encode(bytes(100)).decode("ascii"),
                "mission_object_type_table": base64.b64encode(bytes(100)).decode("ascii"),
                "terrain_grid": base64.b64encode(bytes(256)).decode("ascii"),
                "name_table": base64.b64encode(b"").decode("ascii"),
            }
            (source_root / "VN.WLD").write_bytes(build_wld(wld_payload))

            rc = cli_module.main(["build-soviet-vietnam-pack", str(source_root), str(output_root), "--no-starter-radio", "--package"])
            self.assertEqual(rc, 0)
            self.assertTrue((output_root / "VN" / "VN.WLD.json").exists())
            self.assertTrue((output_root / "SVN" / "SVN.WLD.json").exists())
            self.assertTrue((output_root / "SVN.zip").exists())
            manifest = json.loads((output_root / "SVN" / "campaign.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["id"], "SVN")
            self.assertEqual(manifest["launch"]["argv"], ["--campaign", "SVN"])
            self.assertEqual(manifest["verification_status"]["package_validation"], "passed")
            generated = json.loads((output_root / "SVN" / "SVN.WLD.json").read_text(encoding="utf-8"))
            self.assertEqual(generated["mission_plan"]["objectives"][1]["object_slot"], 6)
            self.assertEqual(generated["world_objects"][6]["scenario_objective_id"], "strike_carrier_group")


if __name__ == "__main__":
    unittest.main()
