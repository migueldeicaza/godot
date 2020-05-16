
supported_platforms = ['windows', 'osx', 'x11', 'server', 'android', 'haiku', 'javascript', 'iphone']


def can_build(env, platform):
    return True


def configure(env):
    platform = env['platform']

    if platform not in supported_platforms:
        raise RuntimeError('This module does not currently support building for this platform')

    env.use_ptrcall = True
    env.add_module_version_string('swift')

def get_doc_classes():
    return [
        #'@Swift',
        #'SwiftScript',
        #'GodotSwift',
    ]


def get_doc_path():
    return 'doc_classes'


def is_enabled():
    # The module is disabled by default. Use module_mono_enabled=yes to enable it.
    return True 
