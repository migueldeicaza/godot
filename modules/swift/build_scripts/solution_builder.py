
import os


verbose = False

def run_command(command, args, env_override=None, name=None):
    def cmd_args_to_str(cmd_args):
        return ' '.join([arg if not ' ' in arg else '"%s"' % arg for arg in cmd_args])

    args = [command] + args

    if name is None:
        name = os.path.basename(command)

    if verbose:
        print("Running '%s': %s" % (name, cmd_args_to_str(args)))

    import subprocess
    try:
        if env_override is None:
            subprocess.check_call(args)
        else:
            subprocess.check_call(args, env=env_override)
    except subprocess.CalledProcessError as e:
        raise RuntimeError("'%s' exited with error code: %s" % (name, e.returncode))


def build_solution(env, solution_path, build_config, extra_msbuild_args=[]):
    global verbose
    verbose = env['verbose']

    swiftbuild_env = os.environ.copy()

    swiftbuild_args = []

    dotnet_cli = find_dotnet_cli()

    swiftbuild_path = "/usr/bin/swift"
    swiftbuild_args = "build"
    print('MSBuild path: ' + swiftbuild_path)

    # Build solution

    targets = ["Restore", "Build"]

    swiftbuild_args += extra_swiftbuild_args

    run_command(swiftbuild_path, swiftbuild_args, env_override=swiftbuild_env, name='msbuild')
