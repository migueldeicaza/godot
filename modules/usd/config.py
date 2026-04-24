def can_build(env, platform):
    return platform in ("linuxbsd", "macos", "windows") and not env["disable_3d"]


def configure(env):
    pass
