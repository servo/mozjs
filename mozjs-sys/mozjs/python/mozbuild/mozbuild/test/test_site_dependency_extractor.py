# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import pytest
from mozunit import main

from mozbuild.lockfiles.site_dependency_extractor import (
    DependencyParseError,
    SiteDependencyExtractor,
)


def test_requirements_txt_parsing(tmp_path):
    sites_dir = tmp_path / "sites"
    sites_dir.mkdir()

    other_requirements_txt = tmp_path / "other-requirements.txt"
    other_requirements_txt.write_text(
        """# Included requirements
chardet==4.0.0 --hash=sha256:jkl012
"""
    )

    requirements_txt = tmp_path / "requirements.txt"
    requirements_txt.write_text(
        """# Comment line
certifi==2021.5.30 --hash=sha256:abc123

# Another comment
urllib3==1.26.5 --hash=sha256:def456
requests==2.26.0 --hash=sha256:ghi789  # inline comment

# Empty line follows

-r other-requirements.txt
--index-url https://pypi.org/simple
"""
    )

    site_file = sites_dir / "test-site.txt"
    site_file.write_text(
        """requires-python:>=3.8
requirements-txt:requirements.txt
"""
    )

    extractor = SiteDependencyExtractor("test-site", sites_dir, tmp_path)
    requires_python, dependencies = extractor.parse()

    assert requires_python == ">=3.8"
    assert len(dependencies) == 4

    dep_names = {dep.name for dep in dependencies}
    assert "certifi" in dep_names
    assert "urllib3" in dep_names
    assert "requests" in dep_names
    assert "chardet" in dep_names

    dep_dict = {dep.name: dep.version for dep in dependencies}
    assert dep_dict["certifi"] == "==2021.5.30 --hash=sha256"
    assert dep_dict["urllib3"] == "==1.26.5 --hash=sha256"
    assert dep_dict["requests"] == "==2.26.0 --hash=sha256"
    assert dep_dict["chardet"] == "==4.0.0 --hash=sha256"


def test_requirements_txt_missing_file(tmp_path):
    sites_dir = tmp_path / "sites"
    sites_dir.mkdir()

    site_file = sites_dir / "test-site.txt"
    site_file.write_text(
        """requires-python:>=3.8
requirements-txt:nonexistent.txt
"""
    )

    extractor = SiteDependencyExtractor("test-site", sites_dir, tmp_path)
    with pytest.raises(DependencyParseError, match="requirements.txt file not found"):
        extractor.parse()


if __name__ == "__main__":
    main()
