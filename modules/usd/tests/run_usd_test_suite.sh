#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_root="$(CDPATH= cd -- "$script_dir/../../.." && pwd)"

godot_bin="${1:-}"
if [[ -z "$godot_bin" ]]; then
	godot_bin="$(find "$repo_root/bin" -maxdepth 1 -type f -name 'godot.*.editor*' | sort | tail -n 1)"
fi

if [[ -z "$godot_bin" || ! -x "$godot_bin" ]]; then
	echo "Unable to find a runnable Godot binary. Pass it explicitly as the first argument." >&2
	exit 1
fi

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/godot-usd-tests.XXXXXX")"
trap 'rm -rf "$tmp_dir"' EXIT

run_probe() {
	local script="$1"
	shift
	echo "Running $(basename "$script")"
	"$godot_bin" --headless --path "$repo_root" --script "$script" -- "$@"
}

variant_stage="$repo_root/tests/data/usd/variant_stage.usda"
vehicle_variants="$repo_root/thirdparty/vehicleVariants.selfcontained.usdz"
blender_fixture_dir="$repo_root/blender-test-data-usd"
variant_stage_usdc="$repo_root/tests/data/usd/variant_stage.usdc"

cp "$repo_root/tests/data/usd/composition_ref_target.usda" "$tmp_dir/composition_ref_target.usda"
cp "$repo_root/tests/data/usd/composition_payload_target.usda" "$tmp_dir/composition_payload_target.usda"

echo "Running USD doctest suite"
"$godot_bin" --test --test-suite='[SceneTree][USD]' --minimal --no-intro

run_probe "$script_dir/variant_probe.gd" "$variant_stage"
run_probe "$script_dir/variant_scene_load_probe.gd" "$variant_stage" "$tmp_dir/variant_scene_load.usdz"
run_probe "$script_dir/variant_boundary_metadata_probe.gd" "$variant_stage"
run_probe "$script_dir/variant_composition_boundary_edit_warning_probe.gd" "$variant_stage" "$tmp_dir/variant_boundary_edit.usda"
run_probe "$script_dir/variant_flattened_save_report_probe.gd" "$variant_stage" "$tmp_dir/variant_flattened.usdz"
run_probe "$script_dir/variant_usd_layer_preserve_probe.gd" "$variant_stage" "$tmp_dir/variant_stage_unchanged.usda" "$tmp_dir/variant_stage_edited.usda"

if [[ -f "$variant_stage_usdc" ]]; then
	run_probe "$script_dir/variant_usdc_preserve_probe.gd" "$variant_stage_usdc" "$tmp_dir/variant_stage_unchanged.usdc" "$tmp_dir/variant_stage_edited.usdc"
else
	echo "Skipping variant_usdc_preserve_probe.gd because $variant_stage_usdc is not present"
fi

run_probe "$script_dir/vehicle_variant_probe.gd" "$vehicle_variants"
run_probe "$script_dir/variant_scale_probe.gd" "$vehicle_variants"
run_probe "$script_dir/variant_usdz_preserve_probe.gd" "$vehicle_variants" "$tmp_dir/vehicle_unchanged.usdz" "$tmp_dir/vehicle_edited.usdz"

run_probe "$script_dir/composition_probe.gd" "$repo_root/tests/data/usd/composition_reference_source.usda" "$tmp_dir/composition_reference_saved.usda" "usd:references" "./composition_ref_target.usda" "/Asset"
run_probe "$script_dir/composition_probe.gd" "$repo_root/tests/data/usd/composition_payload_source.usda" "$tmp_dir/composition_payload_saved.usda" "usd:payloads" "./composition_payload_target.usda" "/PayloadAsset"
run_probe "$script_dir/composition_path_arcs_probe.gd" "$repo_root/tests/data/usd/composition_inherits_source.usda" "$tmp_dir/composition_inherits_saved.usda" "usd:inherits" "/BaseAsset"
run_probe "$script_dir/composition_path_arcs_probe.gd" "$repo_root/tests/data/usd/composition_specializes_source.usda" "$tmp_dir/composition_specializes_saved.usda" "usd:specializes" "/BaseAsset"
run_probe "$script_dir/blend_shape_piecewise_probe.gd"
run_probe "$script_dir/blend_shape_roundtrip_probe.gd" "$tmp_dir/blend_shape_roundtrip.usda"
run_probe "$script_dir/points_blend_shape_roundtrip_probe.gd" "$tmp_dir/points_blend_shape_roundtrip.usda"
run_probe "$script_dir/skeleton_roundtrip_probe.gd" "$tmp_dir/skeleton_roundtrip.usda"
run_probe "$script_dir/skel_animation_sparsity_probe.gd" "$tmp_dir/skel_animation_sparsity.usda"
run_probe "$script_dir/rigging_stress_probe.gd" "$tmp_dir"

if [[ -d "$blender_fixture_dir" ]]; then
	run_probe "$script_dir/skeleton_probe.gd" "$blender_fixture_dir"
	run_probe "$script_dir/basis_curves_probe.gd" "$blender_fixture_dir"
	run_probe "$script_dir/blender_coverage_probe.gd" "$blender_fixture_dir"
else
	echo "Skipping blender_coverage_probe.gd because $blender_fixture_dir is not present"
fi

echo "USD test suite completed"
