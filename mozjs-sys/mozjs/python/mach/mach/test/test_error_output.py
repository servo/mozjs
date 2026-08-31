# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import io
import sys
import traceback
from pathlib import Path

from mozunit import main

from mach.main import COMMAND_ERROR_TEMPLATE, MODULE_ERROR_TEMPLATE, Mach


def test_command_error(run_mach):
    result, stdout, stderr = run_mach(
        ["throw", "--message", "Command Error"], provider_files=Path("throw.py")
    )
    assert result == 1
    assert COMMAND_ERROR_TEMPLATE % "throw" in stdout


def test_invoked_error(run_mach):
    result, stdout, stderr = run_mach(
        ["throw_deep", "--message", "Deep stack"], provider_files=Path("throw.py")
    )
    assert result == 1
    assert MODULE_ERROR_TEMPLATE % "throw_deep" in stdout


def test_print_exception_nested_explicit_chaining():
    """Test that _print_exception properly handles explicit exception chaining (raise ... from ...)."""
    m = Mach(str(Path.cwd()))

    try:
        try:
            raise ValueError("Inner exception with important details")
        except Exception as e:
            raise RuntimeError("Outer exception occurred") from e
    except Exception:
        exc_type, exc_value, exc_tb = sys.exc_info()
        stack = traceback.extract_tb(exc_tb)

        output = io.StringIO()
        m._print_exception(output, exc_type, exc_value, stack)
        result = output.getvalue()

        # Check that both exceptions are shown
        assert "Inner exception with important details" in result
        assert "Outer exception occurred" in result
        assert (
            "The above exception was the direct cause of the following exception"
            in result
        )


def test_print_exception_nested_implicit_chaining():
    """Test that _print_exception properly handles implicit exception chaining."""
    m = Mach(str(Path.cwd()))

    try:
        try:
            raise ValueError("Inner exception details")
        except Exception:
            # This creates implicit chaining (__context__)
            raise RuntimeError("Outer exception")
    except Exception:
        exc_type, exc_value, exc_tb = sys.exc_info()
        stack = traceback.extract_tb(exc_tb)

        output = io.StringIO()
        m._print_exception(output, exc_type, exc_value, stack)
        result = output.getvalue()

        # Check that both exceptions are shown
        assert "Inner exception details" in result
        assert "Outer exception" in result
        assert (
            "During handling of the above exception, another exception occurred"
            in result
        )


def test_print_exception_suppressed_chaining():
    """Test that _print_exception respects suppressed exception chaining (raise ... from None)."""
    m = Mach(str(Path.cwd()))

    try:
        try:
            raise ValueError("Inner exception that should be suppressed")
        except Exception:
            # This suppresses the chain
            raise RuntimeError("Outer exception only") from None
    except Exception:
        exc_type, exc_value, exc_tb = sys.exc_info()
        stack = traceback.extract_tb(exc_tb)

        output = io.StringIO()
        m._print_exception(output, exc_type, exc_value, stack)
        result = output.getvalue()

        # Only outer exception should be shown
        assert "Outer exception only" in result
        assert "Inner exception that should be suppressed" not in result


def test_print_exception_simple():
    """Test that _print_exception still works correctly for simple exceptions."""
    m = Mach(str(Path.cwd()))

    try:
        raise RuntimeError("Simple exception message")
    except Exception:
        exc_type, exc_value, exc_tb = sys.exc_info()
        stack = traceback.extract_tb(exc_tb)

        output = io.StringIO()
        m._print_exception(output, exc_type, exc_value, stack)
        result = output.getvalue()

        # Check that the simple exception is shown
        assert "Simple exception message" in result
        assert "The details of the failure are as follows:" in result


def test_print_exception_fallback_no_traceback():
    """Test that _print_exception falls back to old behavior when traceback is not available."""
    m = Mach(str(Path.cwd()))

    try:
        try:
            raise ValueError("Inner exception")
        except Exception as e:
            raise RuntimeError("Outer exception") from e
    except Exception:
        exc_type, exc_value, exc_tb = sys.exc_info()
        stack = traceback.extract_tb(exc_tb)

        # Although not common, it is possible for exceptions to be in this state
        # eg: if they are created in C or reassigned before being reraised
        exc_value.__traceback__ = None

        output = io.StringIO()
        m._print_exception(output, exc_type, exc_value, stack)
        result = output.getvalue()

        # Should show the exception message and stack but not nested chaining
        assert "Outer exception" in result
        assert "test_print_exception_fallback_no_traceback" in result


def test_nested_exception_output_integration(run_mach):
    """Integration test that nested exceptions are properly displayed in mach command output."""
    result, stdout, stderr = run_mach(
        [
            "throw_nested_explicit",
            "--inner-message",
            "Test inner",
            "--outer-message",
            "Test outer",
        ],
        provider_files=Path("throw_nested.py"),
    )

    assert result == 1
    assert "Test inner" in stdout
    assert "Test outer" in stdout
    assert (
        "The above exception was the direct cause of the following exception" in stdout
    )


def test_implicit_nested_exception_output_integration(run_mach):
    """Integration test for implicit exception chaining in mach command output."""
    result, stdout, stderr = run_mach(
        [
            "throw_nested_implicit",
            "--inner-message",
            "Implicit inner",
            "--outer-message",
            "Implicit outer",
        ],
        provider_files=Path("throw_nested.py"),
    )

    assert result == 1
    assert "Implicit inner" in stdout
    assert "Implicit outer" in stdout
    assert (
        "During handling of the above exception, another exception occurred" in stdout
    )


def test_suppressed_nested_exception_output_integration(run_mach):
    """Integration test for suppressed exception chaining in mach command output."""
    result, stdout, stderr = run_mach(
        [
            "throw_nested_suppressed",
            "--inner-message",
            "InnerMsg",
            "--outer-message",
            "OuterVisible",
        ],
        provider_files=Path("throw_nested.py"),
    )

    assert result == 1
    assert "OuterVisible" in stdout
    # Check that the inner exception message doesn't appear in the traceback section
    # (it might appear in the command line, but not in the exception details)
    traceback_section = stdout[
        stdout.find("The details of the failure are as follows:") :
    ]
    assert "InnerMsg" not in traceback_section


def test_simple_exception_output_integration(run_mach):
    """Integration test to ensure simple exceptions still work correctly."""
    result, stdout, stderr = run_mach(
        ["throw_simple", "--message", "Simple test message"],
        provider_files=Path("throw_nested.py"),
    )

    assert result == 1
    assert "Simple test message" in stdout


if __name__ == "__main__":
    main()
