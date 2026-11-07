"""Admission contract for assertion-based independent mathematical gates."""

import sys


def require_normal_math_mode():
    # This check must itself remain active under -O, -OO and PYTHONOPTIMIZE.
    if sys.flags.optimize != 0:
        raise RuntimeError(
            "Python optimization must be disabled for mathematical qualification"
        )


def interpreter_identity():
    require_normal_math_mode()
    return {
        "executable": sys.executable,
        "version": sys.version,
        "optimize": sys.flags.optimize,
    }
