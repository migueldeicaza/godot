# Build GodotTools Package

import os

from SCons.Script import Dir


def build_godot_tools(source, target, env):
    # source and target elements are of type SCons.Node.FS.File, hence why we convert them to str

    module_dir = env['module_dir']

    solution_path = os.path.join(module_dir, 'editor/GodotTools/Package.swift')
    build_config = 'Debug' if env['target'] == 'debug' else 'Release'

    from .solution_builder import build_solution

    build_solution(env, solution_path, build_config)
    # No need to copy targets. The GodotTools csproj takes care of copying them.


def build(env_swift, api_sln_cmd):
    assert env_swift['tools']

    output_dir = Dir('#bin').abspath
    editor_tools_dir = os.path.join(output_dir, 'GodotSwift', 'Tools')

    target_filenames = ['GodotSwift.dylib']

    targets = [os.path.join(editor_tools_dir, filename) for filename in target_filenames]

    cmd = env_swift.CommandNoCache(targets, api_sln_cmd, build_godot_tools, module_dir=os.getcwd())
    env_swift.AlwaysBuild(cmd)
