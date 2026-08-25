def can_build(env, platform):
    # Desktop platforms only for now. Console ports gate on per-platform
    # certification review (see docs/w4-console-inquiry-checklist.md).
    return platform in ["macos", "linuxbsd", "windows"]


def configure(env):
    pass
