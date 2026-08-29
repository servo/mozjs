# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import os

from mozperftest.layers import Layer

ALLOWED_HOSTS = [
    # Remote Rettings contains the machine learning model descriptions, translations
    # models, and various prompts.
    "firefox.settings.services.mozilla.com",
    "firefox-settings-attachments.cdn.mozilla.net",
    "content-signature-2.cdn.mozilla.net",
    # The models hub is a HuggingFace compatible model hub for downloading models.
    "model-hub.mozilla.org",
    # The MLPA server is the front for the mozilla-managed LLM service.
    "mlpa-prod-prod-mozilla.global.ssl.fastly.net",
    "mlpa-nonprod-stage-mozilla.global.ssl.fastly.net",
]

PREFS = {
    "services.settings.server": "https://firefox.settings.services.mozilla.com/v1",
    "network.socket.allowed_nonlocal_domains": ",".join(ALLOWED_HOSTS),
}


class MLServices(Layer):
    """
    Define environment variable values to allow ML Services to be accessed by tests.
    """

    name = "ml-services"
    activated = True

    def setup(self):
        os.environ["MOZ_REMOTE_SETTINGS_DEVTOOLS"] = "1"

    def run(self, metadata):
        metadata.get_options("browser_prefs").update(PREFS)
        for host in ALLOWED_HOSTS:
            self.info(f"[ml-services] allowed host: {host}")

        return metadata

    def teardown(self):
        os.environ.pop("MOZ_REMOTE_SETTINGS_DEVTOOLS", None)
