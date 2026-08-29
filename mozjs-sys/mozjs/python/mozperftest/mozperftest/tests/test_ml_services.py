import os
import shutil

from mozperftest.environment import SYSTEM
from mozperftest.system.ml_services import ALLOWED_HOSTS, MLServices
from mozperftest.tests.support import get_running_env


def cleanup_ml_services(mach_cmd):
    shutil.rmtree(mach_cmd._mach_context.state_dir)
    if "MOZ_REMOTE_SETTINGS_DEVTOOLS" in os.environ:
        del os.environ["MOZ_REMOTE_SETTINGS_DEVTOOLS"]


def test_ml_services_sets_environment_variables():
    mach_cmd, metadata, env = get_running_env()

    try:
        ml_services = MLServices(env, mach_cmd)

        with ml_services:
            ml_services(metadata)

            assert "MOZ_REMOTE_SETTINGS_DEVTOOLS" in os.environ
            assert os.environ["MOZ_REMOTE_SETTINGS_DEVTOOLS"] == "1"

    finally:
        cleanup_ml_services(mach_cmd)


def test_ml_services_sets_browser_prefs():
    mach_cmd, metadata, env = get_running_env()

    try:
        ml_services = MLServices(env, mach_cmd)

        with ml_services:
            result_metadata = ml_services(metadata)

        browser_prefs = result_metadata.get_options("browser_prefs")
        assert "services.settings.server" in browser_prefs
        assert (
            browser_prefs["services.settings.server"]
            == "https://firefox.settings.services.mozilla.com/v1"
        )

        assert "network.socket.allowed_nonlocal_domains" in browser_prefs
        allowlist = browser_prefs["network.socket.allowed_nonlocal_domains"]
        assert "firefox.settings.services.mozilla.com" in allowlist
        assert "model-hub.mozilla.org" in allowlist
        assert "mlpa-prod-prod-mozilla.global.ssl.fastly.net" in allowlist

    finally:
        cleanup_ml_services(mach_cmd)


def test_ml_services_allowlist_contains_all_expected_hosts():
    expected_hosts = [
        "firefox.settings.services.mozilla.com",
        "firefox-settings-attachments.cdn.mozilla.net",
        "content-signature-2.cdn.mozilla.net",
        "model-hub.mozilla.org",
        "mlpa-prod-prod-mozilla.global.ssl.fastly.net",
    ]

    mach_cmd, metadata, env = get_running_env()

    try:
        ml_services = MLServices(env, mach_cmd)

        with ml_services:
            result_metadata = ml_services(metadata)

            browser_prefs = result_metadata.get_options("browser_prefs")
            allowlist = browser_prefs["network.socket.allowed_nonlocal_domains"]
            for host in expected_hosts:
                assert host in allowlist

    finally:
        cleanup_ml_services(mach_cmd)


def test_ml_services_allowlist_format():
    mach_cmd, metadata, env = get_running_env()

    try:
        ml_services = MLServices(env, mach_cmd)

        with ml_services:
            result_metadata = ml_services(metadata)

            browser_prefs = result_metadata.get_options("browser_prefs")
            allowlist = browser_prefs["network.socket.allowed_nonlocal_domains"]
            hosts = allowlist.split(",")

            assert len(hosts) == len(ALLOWED_HOSTS)

            for host in hosts:
                assert host in ALLOWED_HOSTS

    finally:
        cleanup_ml_services(mach_cmd)


def test_ml_services_layer_properties():
    mach_cmd, metadata, env = get_running_env(flavor="eval-mochitest")

    try:
        system_layers = env.layers[SYSTEM]

        ml_services_layer = None
        for layer in system_layers.layers:
            if isinstance(layer, MLServices):
                ml_services_layer = layer
                break

        assert ml_services_layer is not None
        assert ml_services_layer.name == "ml-services"
        assert ml_services_layer.activated is True

    finally:
        shutil.rmtree(mach_cmd._mach_context.state_dir)


def test_ml_services_returns_metadata():
    mach_cmd, metadata, env = get_running_env()

    try:
        ml_services = MLServices(env, mach_cmd)
        ml_services.setup()
        result = ml_services.run(metadata)

        assert result is metadata

    finally:
        cleanup_ml_services(mach_cmd)


def test_ml_services_preserves_existing_browser_prefs():
    mach_cmd, metadata, env = get_running_env()

    try:
        existing_prefs = metadata.get_options("browser_prefs")
        existing_prefs["some.custom.pref"] = "custom_value"

        ml_services = MLServices(env, mach_cmd)
        ml_services.setup()
        result = ml_services.run(metadata)

        browser_prefs = result.get_options("browser_prefs")
        assert "some.custom.pref" in browser_prefs
        assert browser_prefs["some.custom.pref"] == "custom_value"
        assert "services.settings.server" in browser_prefs

    finally:
        cleanup_ml_services(mach_cmd)


def test_ml_services_setup_sets_remote_settings_devtools():
    mach_cmd, metadata, env = get_running_env()

    try:
        if "MOZ_REMOTE_SETTINGS_DEVTOOLS" in os.environ:
            del os.environ["MOZ_REMOTE_SETTINGS_DEVTOOLS"]

        assert "MOZ_REMOTE_SETTINGS_DEVTOOLS" not in os.environ

        ml_services = MLServices(env, mach_cmd)
        ml_services.setup()

        assert "MOZ_REMOTE_SETTINGS_DEVTOOLS" in os.environ
        assert os.environ["MOZ_REMOTE_SETTINGS_DEVTOOLS"] == "1"

    finally:
        shutil.rmtree(mach_cmd._mach_context.state_dir)
        if "MOZ_REMOTE_SETTINGS_DEVTOOLS" in os.environ:
            del os.environ["MOZ_REMOTE_SETTINGS_DEVTOOLS"]
