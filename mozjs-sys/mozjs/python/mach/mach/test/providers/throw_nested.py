# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from mach.decorators import Command, CommandArgument


@Command("throw_nested_explicit", category="testing")
@CommandArgument("--inner-message", "-i", default="Inner exception message")
@CommandArgument("--outer-message", "-o", default="Outer exception message")
def throw_nested_explicit(command_context, inner_message, outer_message):
    """Throws a nested exception using explicit 'raise ... from ...' syntax."""
    try:
        raise ValueError(inner_message)
    except Exception as e:
        raise RuntimeError(outer_message) from e


@Command("throw_nested_implicit", category="testing")
@CommandArgument("--inner-message", "-i", default="Inner exception message")
@CommandArgument("--outer-message", "-o", default="Outer exception message")
def throw_nested_implicit(command_context, inner_message, outer_message):
    """Throws a nested exception through implicit chaining."""
    try:
        raise ValueError(inner_message)
    except Exception:
        # This creates implicit chaining (__context__)
        raise RuntimeError(outer_message)


@Command("throw_nested_suppressed", category="testing")
@CommandArgument("--inner-message", "-i", default="Inner exception message")
@CommandArgument("--outer-message", "-o", default="Outer exception message")
def throw_nested_suppressed(command_context, inner_message, outer_message):
    """Throws a nested exception but suppresses the chain."""
    try:
        raise ValueError(inner_message)
    except Exception:
        # This suppresses the chain with 'raise ... from None'
        raise RuntimeError(outer_message) from None


@Command("throw_simple", category="testing")
@CommandArgument("--message", "-m", default="Simple exception message")
def throw_simple(command_context, message):
    """Throws a simple exception with no chaining."""
    raise RuntimeError(message)
