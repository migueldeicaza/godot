import shutil

from SCons.Variables import BoolVariable


def can_build(env, platform):
    return (
        platform == "macos"
        and env["arch"] in ("arm64", "x86_64")
        and env["naga_enabled"]
        and shutil.which("cargo") is not None
    )


def configure(env):
    pass


def get_opts(platform):
    return [
        BoolVariable(
            "naga_enabled",
            "Build the experimental Naga GLSL-to-MSL bridge (requires Rust 1.87+)",
            False,
        )
    ]
