#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import re
import shutil
import statistics
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

TIMING_PATTERNS = {
    "forward": re.compile(r"^Naga forward benchmark: (?P<fields>.+)$", re.MULTILINE),
    "all": re.compile(r"^Naga ShaderRD benchmark: (?P<fields>.+)$", re.MULTILINE),
    "builtin": re.compile(r"^Naga ShaderRD benchmark: (?P<fields>.+)$", re.MULTILINE),
}
SAFE_NAME_RE = re.compile(r"[^A-Za-z0-9_.-]+")
VALIDATED_BUILTIN_FAMILIES = {
    "CanvasOcclusionShaderRD",
    "CanvasSdfShaderRD",
    "CanvasShaderRD",
    "ParticlesCopyShaderRD",
    "SceneForwardClusteredShaderRD",
    "SceneForwardMobileShaderRD",
    "SkeletonShaderRD",
    "SkyShaderRD",
}
METAL_FAILURE_MARKERS = (
    "error: 'sampler' attribute parameter is out of bounds",
    "ERROR: Failed to compile Metal library",
    "ERROR: Error compiling shader",
    'Condition "pipeline.is_null()" is true',
    "ERROR: Uniforms supplied for set",
)


@dataclass(frozen=True)
class ShaderTiming:
    shader: str
    variant: int
    backend: str
    translation_us: int
    fallback_stages: int = 0


@dataclass(frozen=True)
class RunResult:
    variants: int
    specialized_variants: int
    translation_us: int
    wall_seconds: float
    project_errors: int
    timings: tuple[ShaderTiming, ...]


def parse_fields(text: str) -> dict[str, str | int]:
    fields: dict[str, str | int] = {}
    for item in text.split():
        key, value = item.split("=", 1)
        fields[key] = int(value) if value.isdigit() else value
    return fields


def is_specialized_variant(method: str, variant: int) -> bool:
    if method == "forward_plus":
        return variant < 9 or (variant >= 18 and variant % 2 == 0)
    return variant < 9 or 18 <= variant < 27


def disable_shader_cache(project_file: Path) -> None:
    contents = project_file.read_text()
    setting = "shader_compiler/shader_cache/enabled=false"
    if re.search(r"^shader_compiler/shader_cache/enabled=.*$", contents, re.MULTILINE):
        contents = re.sub(
            r"^shader_compiler/shader_cache/enabled=.*$",
            setting,
            contents,
            flags=re.MULTILINE,
        )
    elif "[rendering]" in contents:
        contents = contents.replace("[rendering]", f"[rendering]\n\n{setting}", 1)
    else:
        contents += f"\n[rendering]\n\n{setting}\n"
    project_file.write_text(contents)


def run_once(
    binary: Path,
    source_project: Path,
    output_dir: Path,
    method: str,
    backend: str,
    iteration: int,
    frames: int,
    scope: str,
) -> RunResult:
    work_root = Path(tempfile.mkdtemp(prefix="work-", dir=output_dir))
    project = work_root / source_project.name
    try:
        subprocess.run(["cp", "-cR", str(source_project), str(project)], check=True)
        disable_shader_cache(project / "project.godot")

        environment = os.environ.copy()
        environment.pop("GODOT_NAGA_BENCHMARK_FORWARD_VARIANTS", None)
        environment.pop("GODOT_NAGA_BENCHMARK_SHADER_RD", None)
        environment.pop("GODOT_NAGA_BUILTIN_SHADERS", None)
        environment.pop("GODOT_NAGA_TEST_ALL_SHADER_RD", None)
        if scope in ("all", "builtin"):
            environment["GODOT_NAGA_BENCHMARK_SHADER_RD"] = "1"
        else:
            environment["GODOT_NAGA_BENCHMARK_FORWARD_VARIANTS"] = "1"
        environment["GODOT_NAGA_BENCHMARK_NONCE"] = (
            f"{source_project.name}-{method}-{backend}-{iteration}-{time.time_ns()}"
        )
        environment.pop("GODOT_NAGA_UBERSHADERS", None)
        environment.pop("GODOT_NAGA_TEST_ALL_FORWARD_VARIANTS", None)
        if backend == "naga":
            if scope == "all":
                environment["GODOT_NAGA_TEST_ALL_SHADER_RD"] = "1"
                environment.pop("GODOT_NAGA_FORWARD_SHADERS", None)
            elif scope == "builtin":
                environment["GODOT_NAGA_BUILTIN_SHADERS"] = "1"
                environment.pop("GODOT_NAGA_FORWARD_SHADERS", None)
            else:
                environment["GODOT_NAGA_FORWARD_SHADERS"] = "1"
        else:
            environment.pop("GODOT_NAGA_FORWARD_SHADERS", None)

        command = [
            str(binary),
            "--path",
            str(project),
            "--rendering-driver",
            "metal",
            "--rendering-method",
            method,
            "--quit-after",
            str(frames),
            "--verbose",
        ]
        start = time.monotonic()
        result = subprocess.run(
            command,
            env=environment,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=300,
        )
        wall_seconds = time.monotonic() - start
        safe_project = SAFE_NAME_RE.sub("-", source_project.name)
        log_path = output_dir / f"{safe_project}-{method}-{backend}-{iteration + 1}.log"
        log_path.write_text(result.stdout)
        if result.returncode != 0:
            raise RuntimeError(
                f"Godot {source_project.name} {method}/{backend} failed; see {log_path}\n{result.stdout[-4000:]}"
            )
        metal_failures = [marker for marker in METAL_FAILURE_MARKERS if marker in result.stdout]
        if metal_failures:
            raise RuntimeError(
                f"Godot {source_project.name} {method}/{backend} reported Metal shader or pipeline failures "
                f"({', '.join(metal_failures)}); see {log_path}"
            )

        fields = [parse_fields(match.group("fields")) for match in TIMING_PATTERNS[scope].finditer(result.stdout)]
        if scope == "builtin":
            fields = [timing for timing in fields if timing["shader"] in VALIDATED_BUILTIN_FAMILIES]
        timings = tuple(
            ShaderTiming(
                shader=str(timing["shader"]),
                variant=int(timing["variant"]),
                backend=str(timing["backend"]),
                translation_us=int(timing["translation_us"]),
                fallback_stages=int(timing.get("fallback_stages", 0)),
            )
            for timing in fields
        )
        if not timings:
            raise RuntimeError(
                f"Godot {source_project.name} {method}/{backend} compiled no forward variants; see {log_path}"
            )
        specialized_variants = sum(
            timing.shader in ("SceneForwardClusteredShaderRD", "SceneForwardMobileShaderRD")
            and is_specialized_variant(method, timing.variant)
            for timing in timings
        )
        if scope == "forward" and specialized_variants == 0:
            raise RuntimeError(
                f"Godot {source_project.name} {method}/{backend} compiled no specialized variants; see {log_path}"
            )
        unexpected = [timing for timing in timings if timing.backend != backend]
        if scope == "all" and backend == "naga":
            # A census deliberately records unsupported variants that safely used
            # the production GLSLang/SPIRV-Cross fallback.
            unexpected = []
        if unexpected:
            raise RuntimeError(
                f"Godot {source_project.name} {method}/{backend} used a mixed compiler path; see {log_path}"
            )
        if backend == "naga" and scope != "all":
            if "falling back to GLSLang/SPIRV-Cross" in result.stdout:
                raise RuntimeError(f"Naga fell back for {source_project.name} {method}; see {log_path}")
            if "Naga used its GLSLang-to-SPIR-V parser fallback" in result.stdout:
                raise RuntimeError(f"Naga used its parser fallback for {source_project.name} {method}; see {log_path}")

        project_errors = result.stdout.count("SCRIPT ERROR:") + result.stdout.count("ERROR: Failed to load")
        return RunResult(
            variants=len(timings),
            specialized_variants=specialized_variants,
            translation_us=sum(timing.translation_us for timing in timings),
            wall_seconds=wall_seconds,
            project_errors=project_errors,
            timings=timings,
        )
    finally:
        shutil.rmtree(work_root)


def main() -> None:
    repository = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(
        description="Benchmark direct-Naga and legacy Metal translation on real Godot project shader corpora."
    )
    parser.add_argument("projects", nargs="+", type=Path)
    parser.add_argument("--binary", type=Path, default=repository / "bin/godot.macos.editor.arm64")
    parser.add_argument("--method", choices=("forward_plus", "mobile", "all"), default="all")
    parser.add_argument(
        "--scope",
        choices=("forward", "builtin", "all"),
        default="forward",
        help="Benchmark forward shaders, all validated built-ins, or census every ShaderRD family with safe fallback.",
    )
    parser.add_argument("--iterations", type=int, default=3)
    parser.add_argument("--frames", type=int, default=180)
    parser.add_argument("--output-dir", type=Path)
    arguments = parser.parse_args()
    if not arguments.binary.is_file():
        parser.error(f"Godot binary does not exist: {arguments.binary}")
    if arguments.iterations < 1 or arguments.frames < 1:
        parser.error("--iterations and --frames must be positive")
    projects = [project.expanduser().resolve() for project in arguments.projects]
    for project in projects:
        if not (project / "project.godot").is_file():
            parser.error(f"Not a Godot project: {project}")

    output_dir = arguments.output_dir or Path(tempfile.mkdtemp(prefix="godot-naga-corpus-"))
    output_dir.mkdir(parents=True, exist_ok=True)
    methods = ("forward_plus", "mobile") if arguments.method == "all" else (arguments.method,)

    for project in projects:
        for method in methods:
            samples: dict[str, list[RunResult]] = {"naga": [], "legacy": []}
            for iteration in range(arguments.iterations):
                order = ("naga", "legacy") if iteration % 2 == 0 else ("legacy", "naga")
                for backend in order:
                    sample = run_once(
                        arguments.binary,
                        project,
                        output_dir,
                        method,
                        backend,
                        iteration,
                        arguments.frames,
                        arguments.scope,
                    )
                    samples[backend].append(sample)
                    if arguments.scope in ("all", "builtin") and backend == "naga":
                        direct = sum(
                            timing.backend == "naga" and timing.fallback_stages == 0 for timing in sample.timings
                        )
                        parser_fallback = sum(
                            timing.backend == "naga" and timing.fallback_stages > 0 for timing in sample.timings
                        )
                        legacy_fallback = sum(timing.backend == "legacy" for timing in sample.timings)
                        coverage = (
                            f"direct={direct}, parser_fallback={parser_fallback}, legacy_fallback={legacy_fallback}"
                        )
                    else:
                        coverage = f"{sample.specialized_variants} specialized"
                    print(
                        f"{project.name} {method} {backend} run {iteration + 1}: "
                        f"variants={sample.variants} ({coverage}), translation={sample.translation_us / 1000:.3f} ms, "
                        f"wall={sample.wall_seconds:.3f} s, project_errors={sample.project_errors}",
                        flush=True,
                    )

            naga_translation = statistics.median(sample.translation_us for sample in samples["naga"])
            legacy_translation = statistics.median(sample.translation_us for sample in samples["legacy"])
            naga_wall = statistics.median(sample.wall_seconds for sample in samples["naga"])
            legacy_wall = statistics.median(sample.wall_seconds for sample in samples["legacy"])
            print(
                f"{project.name} {method} median ({arguments.iterations} runs): "
                f"translation Naga={naga_translation / 1000:.3f} ms, legacy={legacy_translation / 1000:.3f} ms, "
                f"speedup={legacy_translation / naga_translation:.2f}x; "
                f"process Naga={naga_wall:.3f} s, legacy={legacy_wall:.3f} s, "
                f"speedup={legacy_wall / naga_wall:.2f}x",
                flush=True,
            )

            if arguments.scope in ("all", "builtin"):
                families = sorted(
                    {
                        timing.shader
                        for backend_samples in samples.values()
                        for sample in backend_samples
                        for timing in sample.timings
                    },
                    key=lambda family: -statistics.median(
                        sum(timing.translation_us for timing in sample.timings if timing.shader == family)
                        for sample in samples["legacy"]
                    ),
                )
                print(f"{project.name} {method} per-family census (ordered by legacy translation cost):")
                for family in families:
                    naga_family_samples = [
                        [timing for timing in sample.timings if timing.shader == family] for sample in samples["naga"]
                    ]
                    legacy_family_samples = [
                        [timing for timing in sample.timings if timing.shader == family] for sample in samples["legacy"]
                    ]
                    event_count = int(statistics.median(len(sample) for sample in naga_family_samples))
                    direct_count = int(
                        statistics.median(
                            sum(timing.backend == "naga" and timing.fallback_stages == 0 for timing in sample)
                            for sample in naga_family_samples
                        )
                    )
                    parser_count = int(
                        statistics.median(
                            sum(timing.backend == "naga" and timing.fallback_stages > 0 for timing in sample)
                            for sample in naga_family_samples
                        )
                    )
                    fallback_count = int(
                        statistics.median(
                            sum(timing.backend == "legacy" for timing in sample) for sample in naga_family_samples
                        )
                    )
                    naga_family_us = statistics.median(
                        sum(timing.translation_us for timing in sample) for sample in naga_family_samples
                    )
                    legacy_family_us = statistics.median(
                        sum(timing.translation_us for timing in sample) for sample in legacy_family_samples
                    )
                    print(
                        f"  {family}: variants={event_count}, direct={direct_count}, parser_fallback={parser_count}, "
                        f"legacy_fallback={fallback_count}, Naga={naga_family_us / 1000:.3f} ms, "
                        f"legacy={legacy_family_us / 1000:.3f} ms"
                    )

    print(f"Corpus logs: {output_dir}")


if __name__ == "__main__":
    main()
