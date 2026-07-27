#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
import os
import re
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Capture:
    width: int
    height: int
    pixels: bytes


NAGA_VARIANT_RE = re.compile(r"Compiled Metal forward shader variant \S+:(\d+) with Naga\.")


def is_specialized_variant(method: str, variant: int) -> bool:
    if method == "forward_plus":
        return variant < 9 or (variant >= 18 and variant % 2 == 0)
    return variant < 9 or 18 <= variant < 27


def run_capture(binary: Path, project: Path, output_dir: Path, method: str, backend: str, pipeline: str) -> Capture:
    capture_path = output_dir / f"{method}-{pipeline}-{backend}"
    environment = os.environ.copy()
    environment["GODOT_NAGA_RENDER_CAPTURE"] = str(capture_path)
    environment["GODOT_NAGA_RENDER_CAPTURE_FRAMES"] = "8"
    environment.pop("GODOT_NAGA_TEST_ALL_UBER_VARIANTS", None)
    environment.pop("GODOT_NAGA_BENCHMARK_UBER_VARIANTS", None)
    environment.pop("GODOT_NAGA_BENCHMARK_NONCE", None)
    if pipeline == "uber":
        environment["GODOT_NAGA_FORCE_UBERSHADERS"] = "1"
        environment.pop("GODOT_NAGA_FORCE_SPECIALIZED_SHADERS", None)
    else:
        environment.pop("GODOT_NAGA_FORCE_UBERSHADERS", None)
        environment["GODOT_NAGA_FORCE_SPECIALIZED_SHADERS"] = "1"
    if backend == "naga":
        if pipeline == "uber":
            environment["GODOT_NAGA_UBERSHADERS"] = "1"
            environment.pop("GODOT_NAGA_FORWARD_SHADERS", None)
        else:
            environment["GODOT_NAGA_FORWARD_SHADERS"] = "1"
            environment.pop("GODOT_NAGA_UBERSHADERS", None)
    else:
        environment.pop("GODOT_NAGA_UBERSHADERS", None)
        environment.pop("GODOT_NAGA_FORWARD_SHADERS", None)

    command = [
        str(binary),
        "--path",
        str(project),
        "--rendering-driver",
        "metal",
        "--rendering-method",
        method,
        "--verbose",
    ]
    result = subprocess.run(
        command,
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=180,
    )
    log_path = capture_path.with_suffix(".log")
    log_path.write_text(result.stdout)
    if result.returncode != 0:
        raise RuntimeError(f"Godot {method}/{backend} failed; see {log_path}\n{result.stdout[-4000:]}")
    if "Naga render capture:" not in result.stdout:
        raise RuntimeError(f"Godot {method}/{backend} emitted no completed capture; see {log_path}")
    if pipeline == "uber" and "Ubershaders: Forced by GODOT_NAGA_FORCE_UBERSHADERS" not in result.stdout:
        raise RuntimeError(f"Godot {method}/{backend} did not confirm the forced Uber path; see {log_path}")
    if (
        pipeline == "specialized"
        and "Specialized shaders: Forced by GODOT_NAGA_FORCE_SPECIALIZED_SHADERS" not in result.stdout
    ):
        raise RuntimeError(f"Godot {method}/{backend} did not confirm the forced specialized path; see {log_path}")
    if backend == "naga":
        if "Compiled Metal forward shader variant" not in result.stdout or " with Naga." not in result.stdout:
            raise RuntimeError(f"Godot {method}/naga did not report a Naga-compiled forward shader; see {log_path}")
        if "falling back to GLSLang/SPIRV-Cross" in result.stdout:
            raise RuntimeError(f"Godot {method}/naga used the legacy fallback; see {log_path}")
        variants = [int(match.group(1)) for match in NAGA_VARIANT_RE.finditer(result.stdout)]
        if pipeline == "specialized" and not any(is_specialized_variant(method, variant) for variant in variants):
            raise RuntimeError(f"Godot {method}/naga did not report a specialized Naga variant; see {log_path}")
    elif " with Naga." in result.stdout:
        raise RuntimeError(f"Godot {method}/legacy unexpectedly used Naga; see {log_path}")

    size_path = capture_path.with_suffix(".size")
    raw_path = capture_path.with_suffix(".rgba8")
    if not size_path.is_file() or not raw_path.is_file():
        raise RuntimeError(f"Godot {method}/{backend} did not write the raw capture files; see {log_path}")
    width, height = (int(value) for value in size_path.read_text().split())
    pixels = raw_path.read_bytes()
    expected_size = width * height * 4
    if len(pixels) != expected_size:
        raise RuntimeError(
            f"Godot {method}/{backend} wrote {len(pixels)} bytes for a {width}x{height} RGBA8 image "
            f"(expected {expected_size})"
        )
    if max(pixels) - min(pixels) < 16 or len(set(pixels)) < 32:
        raise RuntimeError(f"Godot {method}/{backend} produced a suspiciously uniform image")
    return Capture(width, height, pixels)


def write_diff(path: Path, expected: Capture, actual: Capture) -> None:
    rgb = bytearray(expected.width * expected.height * 3)
    for pixel in range(expected.width * expected.height):
        source = pixel * 4
        target = pixel * 3
        for channel in range(3):
            difference = abs(expected.pixels[source + channel] - actual.pixels[source + channel])
            rgb[target + channel] = min(255, difference * 16)
    with path.open("wb") as image:
        image.write(f"P6\n{expected.width} {expected.height}\n255\n".encode())
        image.write(rgb)


def compare(expected: Capture, actual: Capture) -> tuple[int, float, float, int, float]:
    if (expected.width, expected.height) != (actual.width, actual.height):
        raise RuntimeError(
            f"Capture dimensions differ: legacy={expected.width}x{expected.height}, Naga={actual.width}x{actual.height}"
        )
    differences = [abs(left - right) for left, right in zip(expected.pixels, actual.pixels)]
    pixel_count = expected.width * expected.height
    differing_pixels = sum(
        expected.pixels[offset : offset + 4] != actual.pixels[offset : offset + 4]
        for offset in range(0, len(expected.pixels), 4)
    )
    maximum = max(differences)
    mean = sum(differences) / len(differences)
    rms = math.sqrt(sum(value * value for value in differences) / len(differences))
    return maximum, mean, rms, differing_pixels, differing_pixels * 100.0 / pixel_count


def main() -> None:
    repository = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(description="Compare legacy and direct-Naga Metal forward-shader rendering.")
    parser.add_argument("--binary", type=Path, default=repository / "bin/godot.macos.editor.arm64")
    parser.add_argument("--method", choices=("forward_plus", "mobile", "all"), default="all")
    parser.add_argument("--pipeline", choices=("uber", "specialized", "all"), default="all")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--max-channel-difference", type=int, default=8)
    parser.add_argument("--max-mean-difference", type=float, default=0.02)
    parser.add_argument("--max-rms-difference", type=float, default=0.2)
    arguments = parser.parse_args()

    if not arguments.binary.is_file():
        parser.error(f"Godot binary does not exist: {arguments.binary}")
    output_dir = arguments.output_dir or Path(tempfile.mkdtemp(prefix="godot-naga-render-"))
    output_dir.mkdir(parents=True, exist_ok=True)
    project = repository / "modules/naga/tests/metal_smoke"
    methods = ("forward_plus", "mobile") if arguments.method == "all" else (arguments.method,)
    pipelines = ("uber", "specialized") if arguments.pipeline == "all" else (arguments.pipeline,)

    failed = False
    for method in methods:
        for pipeline in pipelines:
            # Run legacy first so asynchronous pipeline compilation has the same cold-process
            # setup while Naga remains the image treated as the candidate implementation.
            legacy = run_capture(arguments.binary, project, output_dir, method, "legacy", pipeline)
            naga = run_capture(arguments.binary, project, output_dir, method, "naga", pipeline)
            maximum, mean, rms, differing_pixels, differing_percent = compare(legacy, naga)
            diff_path = output_dir / f"{method}-{pipeline}-diff.ppm"
            write_diff(diff_path, legacy, naga)
            print(
                f"{method}/{pipeline}: max={maximum}, mean={mean:.6f}, rms={rms:.6f}, "
                f"differing_pixels={differing_pixels}/{legacy.width * legacy.height} ({differing_percent:.3f}%)"
            )
            if (
                maximum > arguments.max_channel_difference
                or mean > arguments.max_mean_difference
                or rms > arguments.max_rms_difference
            ):
                failed = True
                print(f"{method}/{pipeline}: FAILED tolerance; amplified RGB difference image: {diff_path}")
            else:
                print(f"{method}/{pipeline}: PASS")

    print(f"Capture artifacts: {output_dir}")
    if failed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
