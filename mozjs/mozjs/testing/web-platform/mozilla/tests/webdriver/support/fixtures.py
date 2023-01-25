import json
import os
import socket
import time
from contextlib import suppress
from urllib.parse import urlparse

import pytest
import webdriver
from mozprocess import ProcessHandler
from mozprofile import Profile
from mozrunner import FirefoxRunner

from support.network import get_free_port


@pytest.fixture(scope="module")
def browser(full_configuration):
    """Start a Firefox instance without using geckodriver.

    geckodriver will automatically use the --remote-allow-hosts and
    --remote.allow.origins command line arguments.

    Starting Firefox without geckodriver allows to set those command line arguments
    as needed. The fixture method returns the browser instance that should be used
    to connect to a RemoteAgent supported protocol (CDP, WebDriver BiDi).
    """
    current_browser = None

    def _browser(use_bidi=False, use_cdp=False, extra_args=None, extra_prefs=None):
        nonlocal current_browser

        # If the requested preferences and arguments match the ones for the
        # already started firefox, we can reuse the current firefox instance,
        # return the instance immediately.
        if current_browser:
            if (
                current_browser.use_bidi == use_bidi
                and current_browser.use_cdp == use_cdp
                and current_browser.extra_args == extra_args
                and current_browser.extra_prefs == extra_prefs
                and current_browser.is_running
            ):
                return current_browser

            # Otherwise, if firefox is already started, terminate it because we need
            # to create a new instance for the provided preferences.
            current_browser.quit()

        firefox_options = full_configuration["capabilities"]["moz:firefoxOptions"]
        current_browser = Browser(
            firefox_options,
            use_bidi=use_bidi,
            use_cdp=use_cdp,
            extra_args=extra_args,
            extra_prefs=extra_prefs,
        )
        current_browser.start()
        return current_browser

    yield _browser

    # Stop firefox at the end of the test module.
    if current_browser is not None:
        current_browser.quit()
        current_browser = None


@pytest.fixture
def custom_profile(configuration):
    # Clone the known profile for automation preferences
    firefox_options = configuration["capabilities"]["moz:firefoxOptions"]
    _, profile_folder = firefox_options["args"]
    profile = Profile.clone(profile_folder)

    yield profile

    profile.cleanup()


@pytest.fixture
def geckodriver(configuration):
    """Start a geckodriver instance directly."""
    driver = None

    def _geckodriver(config=None, hostname=None, extra_args=None):
        nonlocal driver

        if config is None:
            config = configuration

        driver = Geckodriver(config, hostname, extra_args)
        driver.start()

        return driver

    yield _geckodriver

    if driver is not None:
        driver.stop()


class Browser:
    def __init__(
        self,
        firefox_options,
        use_bidi=False,
        use_cdp=False,
        extra_args=None,
        extra_prefs=None,
    ):
        self.use_bidi = use_bidi
        self.bidi_port_file = None
        self.use_cdp = use_cdp
        self.cdp_port_file = None
        self.extra_args = extra_args
        self.extra_prefs = extra_prefs

        self.debugger_address = None
        self.remote_agent_host = None
        self.remote_agent_port = None

        # Prepare temporary profile
        _profile_arg, profile_folder = firefox_options["args"]
        self.profile = Profile.clone(profile_folder)
        if self.extra_prefs is not None:
            self.profile.set_preferences(self.extra_prefs)

        if use_cdp:
            self.cdp_port_file = os.path.join(
                self.profile.profile, "DevToolsActivePort"
            )
            with suppress(FileNotFoundError):
                os.remove(self.cdp_port_file)
        if use_bidi:
            self.webdriver_bidi_file = os.path.join(
                self.profile.profile, "WebDriverBiDiServer.json"
            )
            with suppress(FileNotFoundError):
                os.remove(self.webdriver_bidi_file)

        # Prepare Firefox runner
        binary = firefox_options["binary"]

        cmdargs = ["-no-remote"]
        if self.use_bidi or self.use_cdp:
            cmdargs.extend(["--remote-debugging-port", "0"])
        if self.extra_args is not None:
            cmdargs.extend(self.extra_args)
        self.runner = FirefoxRunner(
            binary=binary, profile=self.profile, cmdargs=cmdargs
        )

    @property
    def is_running(self):
        return self.runner.is_running()

    def start(self):
        # Start Firefox.
        self.runner.start()

        if self.use_bidi:
            # Wait until the WebDriverBiDiServer.json file is ready
            while not os.path.exists(self.webdriver_bidi_file):
                time.sleep(0.1)

            # Read the connection details from file
            data = json.loads(open(self.webdriver_bidi_file).read())
            self.remote_agent_host = data["ws_host"]
            self.remote_agent_port = int(data["ws_port"])

        if self.use_cdp:
            # Wait until the DevToolsActivePort file is ready
            while not os.path.exists(self.cdp_port_file):
                time.sleep(0.1)

            # Read the port if needed and the debugger address from the
            # DevToolsActivePort file
            lines = open(self.cdp_port_file).readlines()
            assert len(lines) == 2

            if self.remote_agent_port is None:
                self.remote_agent_port = int(lines[0].strip())
            self.debugger_address = lines[1].strip()

    def quit(self, clean_profile=True):
        if self.is_running:
            self.runner.stop()
            self.runner.cleanup()

        if clean_profile:
            self.profile.cleanup()


class Geckodriver:
    def __init__(self, configuration, hostname=None, extra_args=None):
        self.config = configuration["webdriver"]
        self.requested_capabilities = configuration["capabilities"]
        self.hostname = hostname or configuration["host"]
        self.extra_args = extra_args or []

        self.command = None
        self.proc = None
        self.port = get_free_port()

        capabilities = {"alwaysMatch": self.requested_capabilities}
        self.session = webdriver.Session(
            self.hostname, self.port, capabilities=capabilities
        )

    @property
    def remote_agent_port(self):
        webSocketUrl = self.session.capabilities.get("webSocketUrl")
        assert webSocketUrl is not None

        return urlparse(webSocketUrl).port

    def start(self):
        self.command = (
            [self.config["binary"], "--port", str(self.port)]
            + self.config["args"]
            + self.extra_args
        )

        def processOutputLine(line):
            print(line)

        print(f"Running command: {self.command}")
        self.proc = ProcessHandler(
            self.command, processOutputLine=processOutputLine, universal_newlines=True
        )
        self.proc.run()

        # Wait for the port to become ready
        end_time = time.time() + 10
        while time.time() < end_time:
            if self.proc.poll() is not None:
                raise Exception(f"geckodriver terminated with code {self.proc.poll()}")
            with socket.socket() as sock:
                if sock.connect_ex((self.hostname, self.port)) == 0:
                    break
        else:
            raise Exception(
                f"Failed to connect to geckodriver on {self.hostname}:{self.port}"
            )

        return self

    def stop(self):
        self.delete_session()

        if self.proc:
            self.proc.kill()

    def new_session(self):
        self.session.start()

    def delete_session(self):
        self.session.end()
