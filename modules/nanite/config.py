def can_build(env, platform):
    # The build loop is a thin layer over the bundled meshoptimizer 1.2.
    env.module_add_dependencies("nanite", ["meshoptimizer"])
    return not env["disable_3d"]


def configure(env):
    pass
