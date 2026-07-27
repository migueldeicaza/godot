#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import re
import statistics
import subprocess
import time
from pathlib import Path

BENCHMARK_RE = re.compile(r"^Naga benchmark: (?P<fields>.+)$", re.MULTILINE)
COVERAGE_RE = re.compile(r"Metal pipelines (?P<passed>\d+)/(?P<total>\d+) \((?P<failed>\d+) failed\)")


def parse_fields(output: str) -> dict[str, str | int]:
    match = BENCHMARK_RE.search(output)
    if match is None:
        raise RuntimeError("Godot emitted no Naga benchmark summary")

    fields: dict[str, str | int] = {}
    for item in match.group("fields").split():
        key, value = item.split("=", 1)
        fields[key] = int(value) if value.isdigit() else value
    return fields


def run_once(binary: Path, project: Path, method: str, backend: str, iteration: int) -> dict[str, str | int]:
    environment = os.environ.copy()
    environment["GODOT_NAGA_TEST_ALL_UBER_VARIANTS"] = "1"
    environment["GODOT_NAGA_BENCHMARK_UBER_VARIANTS"] = "1"
    environment["GODOT_NAGA_BENCHMARK_NONCE"] = f"{method}-{backend}-{iteration}-{time.time_ns()}"
    if backend == "naga":
        environment["GODOT_NAGA_UBERSHADERS"] = "1"
    else:
        environment.pop("GODOT_NAGA_UBERSHADERS", None)

    command = [
        str(binary),
        "--path",
        str(project),
        "--rendering-driver",
        "metal",
        "--rendering-method",
        method,
        "--quit-after",
        "2",
    ]
    result = subprocess.run(command, env=environment, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        raise RuntimeError(f"Godot {method}/{backend} failed:\n{result.stdout}")

    coverage = COVERAGE_RE.search(result.stdout)
    if coverage is None or coverage.group("passed") != coverage.group("total") or coverage.group("failed") != "0":
        raise RuntimeError(f"Godot {method}/{backend} did not create every Metal pipeline:\n{result.stdout}")

    fields = parse_fields(result.stdout)
    if fields.get("backend") != backend:
        raise RuntimeError(f"Expected {backend} benchmark data, received {fields.get('backend')}")
    expected_variants = 25 if method == "forward_plus" else 18
    if fields.get("variants") != expected_variants:
        raise RuntimeError(f"Expected {expected_variants} {method} variants, received {fields.get('variants')}")
    if fields.get("naga_fallback_stages") != 0:
        raise RuntimeError(f"Naga unexpectedly used {fields.get('naga_fallback_stages')} fallback stages")
    return fields


def median(samples: list[dict[str, str | int]], field: str) -> float:
    return statistics.median(float(sample[field]) for sample in samples)


def main() -> None:
    repository = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(description="Compare cold-cache Naga and legacy Metal Uber-shader compilation.")
    parser.add_argument("--binary", type=Path, default=repository / "bin/godot.macos.editor.arm64")
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--method", choices=("forward_plus", "mobile", "all"), default="all")
    arguments = parser.parse_args()
    if arguments.iterations < 1:
        parser.error("--iterations must be positive")

    methods = ("forward_plus", "mobile") if arguments.method == "all" else (arguments.method,)
    project = repository / "modules/naga/tests/metal_smoke"

    for method in methods:
        samples = {"naga": [], "legacy": []}
        for iteration in range(arguments.iterations):
            order = ("naga", "legacy") if iteration % 2 == 0 else ("legacy", "naga")
            for backend in order:
                fields = run_once(arguments.binary, project, method, backend, iteration)
                samples[backend].append(fields)
                print(
                    f"{method} {backend} run {iteration + 1}: "
                    f"translation={float(fields['translation_us']) / 1000:.3f} ms, "
                    f"pipelines={float(fields['pipeline_us']) / 1000:.3f} ms",
                    flush=True,
                )

        naga_translation = median(samples["naga"], "translation_us")
        legacy_translation = median(samples["legacy"], "translation_us")
        naga_pipeline = median(samples["naga"], "pipeline_us")
        legacy_pipeline = median(samples["legacy"], "pipeline_us")
        naga_parse = median(samples["naga"], "naga_parse_us")
        naga_reflect = median(samples["naga"], "naga_reflect_us")
        naga_msl = median(samples["naga"], "naga_msl_us")
        legacy_glslang = median(samples["legacy"], "glslang_us")
        legacy_container = median(samples["legacy"], "legacy_container_us")
        print(
            f"{method} median ({arguments.iterations} runs): "
            f"translation Naga={naga_translation / 1000:.3f} ms, legacy={legacy_translation / 1000:.3f} ms, "
            f"speedup={legacy_translation / naga_translation:.2f}x; "
            f"Metal pipelines Naga={naga_pipeline / 1000:.3f} ms, legacy={legacy_pipeline / 1000:.3f} ms, "
            f"speedup={legacy_pipeline / naga_pipeline:.2f}x",
            flush=True,
        )
        print(
            f"{method} median stages: "
            f"Naga parse+validate={naga_parse / 1000:.3f} ms, reflection={naga_reflect / 1000:.3f} ms, "
            f"MSL={naga_msl / 1000:.3f} ms; GLSLang={legacy_glslang / 1000:.3f} ms, "
            f"legacy reflection+SPIRV-Cross={legacy_container / 1000:.3f} ms",
            flush=True,
        )


if __name__ == "__main__":
    main()
