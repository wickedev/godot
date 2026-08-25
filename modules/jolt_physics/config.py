def can_build(env, platform):
    return not env["disable_physics_3d"]


def get_opts(platform):
    from SCons.Variables import EnumVariable

    return [
        EnumVariable(
            "jolt_hair_compute",
            "Compute backend for the Jolt strand-hair solver. Off by default: the only backend "
            "wired up so far is 'cpu', which upstream documents as debug-only and does not "
            "optimize for performance",
            "none",
            ("none", "cpu"),
        ),
    ]


def configure(env):
    pass
