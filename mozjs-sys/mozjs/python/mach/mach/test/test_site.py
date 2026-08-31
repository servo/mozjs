# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import subprocess
import sys
from unittest import mock

import pytest
from buildconfig import topsrcdir
from mozunit import main

from mach.requirements import MachEnvRequirements, RequirementsTxtSpecifier
from mach.site import (
    PIP_NETWORK_INSTALL_RESTRICTED_VIRTUALENVS,
    ExternalPythonSite,
    MozSiteMetadata,
    PythonVirtualenv,
    SitePackagesSource,
    _create_venv_with_pthfile,
    resolve_requirements,
)


@pytest.mark.parametrize(
    "env_native_package_source,env_use_system_python,env_moz_automation,expected",
    [
        ("system", False, False, SitePackagesSource.SYSTEM),
        ("pip", False, False, SitePackagesSource.VENV),
        ("none", False, False, SitePackagesSource.NONE),
        (None, False, False, SitePackagesSource.VENV),
        (None, False, True, SitePackagesSource.NONE),
        (None, True, False, SitePackagesSource.NONE),
        (None, True, True, SitePackagesSource.NONE),
    ],
)
def test_resolve_package_source(
    env_native_package_source, env_use_system_python, env_moz_automation, expected
):
    with mock.patch.dict(
        os.environ,
        {
            "MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE": env_native_package_source or "",
            "MACH_USE_SYSTEM_PYTHON": "1" if env_use_system_python else "",
            "MOZ_AUTOMATION": "1" if env_moz_automation else "",
        },
    ):
        assert SitePackagesSource.for_mach() == expected


def test_all_restricted_sites_dont_have_pypi_requirements():
    for site_name in PIP_NETWORK_INSTALL_RESTRICTED_VIRTUALENVS:
        requirements = resolve_requirements(topsrcdir, site_name)
        assert not requirements.pypi_requirements, (
            'Sites that must be able to operate without "pip install" must not have any '
            f'mandatory "pypi requirements". However, the "{site_name}" site depends on: '
            f"{requirements.pypi_requirements}"
        )


@pytest.fixture
def parse_requirements_txt(tmp_path):
    """Fixture to test requirements-txt parsing."""

    def inner(site_content, requirements_txt_content=None):
        # Create site packages file
        site_file = tmp_path / "test-site.txt"
        site_file.write_text(site_content)

        # Create requirements.txt if provided
        if requirements_txt_content is not None:
            req_file = tmp_path / "requirements.txt"
            req_file.write_text(requirements_txt_content)

        # Parse the site file
        requirements = MachEnvRequirements.from_requirements_definition(
            str(tmp_path),
            is_thunderbird=False,
            only_strict_requirements=False,
            requirements_definition=str(site_file),
        )
        return requirements

    return inner


def test_requirements_txt_parsed(parse_requirements_txt):
    requirements = parse_requirements_txt(
        "requires-python:>=3.9\nrequirements-txt:requirements.txt\n",
        "package1==1.0.0 --hash=sha256:abc123\npackage2==2.0.0 --hash=sha256:def456\n",
    )

    assert len(requirements.requirements_txt_files) == 1
    assert "requirements.txt" in requirements.requirements_txt_files[0].path


def test_requirements_txt_missing_file(parse_requirements_txt):
    with pytest.raises(Exception, match="requirements.txt file not found"):
        parse_requirements_txt(
            "requires-python:>=3.8\nrequirements-txt:nonexistent.txt\n"
        )


@pytest.fixture
def run_create_venv_with_pthfile(tmp_path):

    def inner(requirements_content):
        req_file = tmp_path / "requirements.txt"
        req_file.write_text(requirements_content)

        requirements = MachEnvRequirements()
        requirements.requirements_txt_files.append(
            RequirementsTxtSpecifier(str(req_file))
        )

        venv_root = tmp_path / "venv"
        external_python = ExternalPythonSite(sys.executable)
        metadata = MozSiteMetadata(
            sys.hexversion,
            "test",
            SitePackagesSource.VENV,
            external_python,
            str(venv_root),
        )

        venv = PythonVirtualenv(str(venv_root))
        _create_venv_with_pthfile(venv, [], True, requirements, metadata)

        return venv

    return inner


def test_requirements_txt_install_requires_hashes(
    monkeypatch, run_create_venv_with_pthfile
):
    monkeypatch.delenv("MACH_SHOW_PIP_OUTPUT", raising=False)

    try:
        run_create_venv_with_pthfile("certifi==2021.5.30\n")
        pytest.fail("Expected CalledProcessError to be raised due to missing hashes")
    except subprocess.CalledProcessError as e:
        error_output = e.stderr if e.stderr else ""
        assert "hash" in error_output.lower(), (
            f"Expected hash error in stderr, got: {error_output}"
        )


def test_requirements_txt_installs_with_hashes(run_create_venv_with_pthfile):
    requirements_content = (
        "certifi==2021.5.30 "
        "--hash=sha256:50b1e4f8446b06f41be7dd6338db18e0990601dce795c2b1686458aa7e8fa7d8\n"
    )

    venv = run_create_venv_with_pthfile(requirements_content)

    site_packages = venv.resolve_sysconfig_packages_path("purelib")
    assert os.path.exists(site_packages), f"site-packages not found: {site_packages}"

    certifi_path = os.path.join(site_packages, "certifi")
    assert os.path.exists(certifi_path), f"certifi package not found in {site_packages}"


if __name__ == "__main__":
    main()
