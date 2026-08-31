# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import pytest
from mozunit import main

from mozboot.android import ensure_gradle_jdk_installations

KEY = "org.gradle.java.installations.paths"


@pytest.fixture
def mozbuild(tmp_path, monkeypatch):
    path = tmp_path / ".mozbuild"
    path.mkdir()
    monkeypatch.setattr("mozboot.android.MOZBUILD_PATH", path)
    return path


@pytest.fixture
def gradle_props(tmp_path):
    return tmp_path / "gradle.properties"


@pytest.fixture
def make_jdk(mozbuild):
    def _make_jdk(name):
        jdk = mozbuild / "jdk" / name
        jdk.mkdir(parents=True)
        return jdk

    return _make_jdk


@pytest.fixture
def jdk17(make_jdk):
    return make_jdk("jdk-17")


@pytest.fixture
def jdk17_posix(jdk17):
    return jdk17.resolve().as_posix()


@pytest.fixture
def run(gradle_props):
    def _run(new_jdk_home):
        ensure_gradle_jdk_installations(new_jdk_home, gradle_props)

    return _run


def read_paths(gradle_props):
    for line in gradle_props.read_text(encoding="utf-8").splitlines():
        if KEY in line:
            return [p.strip() for p in line.split("=", 1)[1].split(",")]
    return []


def write_jdk_paths(gradle_props, *paths):
    gradle_props.write_text(f"{KEY}={','.join(paths)}\n", encoding="utf-8")


def test_creates_file_if_missing(run, gradle_props, jdk17, jdk17_posix):
    run(jdk17)
    assert gradle_props.exists()
    paths = read_paths(gradle_props)
    assert len(paths) == 1
    assert paths[0] == jdk17_posix


def test_adds_to_empty_file(run, gradle_props, jdk17, jdk17_posix):
    gradle_props.write_text("", encoding="utf-8")
    run(jdk17)
    paths = read_paths(gradle_props)
    assert len(paths) == 1
    assert paths[0] == jdk17_posix


def test_preserves_other_properties(run, gradle_props, jdk17):
    gradle_props.write_text(
        "some.property=hello\nanother.property=world\n", encoding="utf-8"
    )
    run(jdk17)
    content = gradle_props.read_text(encoding="utf-8")
    assert "some.property" in content
    assert "hello" in content
    assert "another.property" in content
    assert "world" in content


def test_purges_stale_mozbuild_paths(run, gradle_props, mozbuild, jdk17, jdk17_posix):
    stale = mozbuild / "jdk" / "jdk-old"
    write_jdk_paths(gradle_props, stale.resolve().as_posix())
    run(jdk17)
    paths = read_paths(gradle_props)
    assert len(paths) == 1
    assert paths[0] == jdk17_posix


def test_keeps_existing_mozbuild_paths(run, gradle_props, make_jdk, jdk17, jdk17_posix):
    jdk21 = make_jdk("jdk-21")
    write_jdk_paths(gradle_props, jdk21.resolve().as_posix())
    run(jdk17)
    paths = read_paths(gradle_props)
    assert len(paths) == 2
    assert jdk17_posix in paths
    assert jdk21.resolve().as_posix() in paths


def test_preserves_non_mozbuild_paths(run, gradle_props, jdk17, jdk17_posix):
    write_jdk_paths(gradle_props, "/usr/lib/jvm/java-17")
    run(jdk17)
    paths = read_paths(gradle_props)
    assert "/usr/lib/jvm/java-17" in paths
    assert jdk17_posix in paths


def test_no_duplicates_on_repeated_calls(run, gradle_props, jdk17):
    run(jdk17)
    run(jdk17)
    paths = read_paths(gradle_props)
    assert len(paths) == 1


def test_handles_spaces_around_equals(run, gradle_props, jdk17, jdk17_posix):
    gradle_props.write_text(f"{KEY} = /usr/lib/jvm/java-17\n", encoding="utf-8")
    run(jdk17)
    paths = read_paths(gradle_props)
    assert "/usr/lib/jvm/java-17" in paths
    assert jdk17_posix in paths


if __name__ == "__main__":
    main()
