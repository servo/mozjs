# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import shutil
import subprocess
import tempfile
import urllib

import mozfile
import requests
import urllib3


class HttpError(Exception):
    pass


class BaseHost:
    MAX_RETRIES_DEFAULT = urllib3.util.Retry(
        total=5, backoff_factor=1, status_forcelist=[429, 500, 502, 503, 504]
    )

    def __init__(self, manifest, max_retries=MAX_RETRIES_DEFAULT):
        self.manifest = manifest
        self.repo_url = urllib.parse.urlparse(self.manifest["vendoring"]["url"])
        adapter = requests.adapters.HTTPAdapter(max_retries=max_retries)
        self.session = requests.Session()
        self.session.mount("http://", adapter)
        self.session.mount("https://", adapter)

    def upstream_tag(self, revision):
        """Temporarily clone the repo to get the latest tag and timestamp"""
        with tempfile.TemporaryDirectory() as temp_repo_clone:
            starting_directory = os.getcwd()
            os.chdir(temp_repo_clone)
            subprocess.run(
                [
                    "git",
                    "clone",
                    "-c",
                    "core.autocrlf=input",
                    self.manifest["vendoring"]["url"],
                    self.manifest["origin"]["name"],
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
                check=True,
            )
            os.chdir("/".join([temp_repo_clone, self.manifest["origin"]["name"]]))
            revision_arg = []
            if revision and revision != "HEAD":
                revision_arg = [revision]

            try:
                tag = subprocess.run(
                    ["git", "--no-pager", "tag", "-l", "--sort=creatordate"]
                    + revision_arg,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    universal_newlines=True,
                    check=True,
                ).stdout.splitlines()[-1]
            except IndexError:  # 0 lines of output, the tag does not exist
                if revision:
                    raise Exception(f"Requested tag {revision} not found in source.")
                else:
                    raise Exception("No tags found in source.")

            tag_timestamp = subprocess.run(
                [
                    "git",
                    "log",
                    "-1",
                    "--date=iso8601-strict",
                    "--format=%cd",
                    tag,
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
                check=True,
            ).stdout.splitlines()[-1]
            os.chdir(starting_directory)
            return tag, tag_timestamp

    def upstream_snapshot(self, revision):
        raise Exception("Unimplemented for this subclass...")

    def upstream_path_to_file(self, revision, filepath):
        raise Exception("Unimplemented for this subclass...")

    def upstream_release_artifact(self, revision, release_artifact):
        raise Exception("Unimplemented for this subclass...")

    def _transform_single_file_to_destination(self, from_file, destination):
        shutil.copy2(from_file.name, destination)

    def download_single_file(self, url, destination):
        response = self.session.get(url, stream=True)
        if response.status_code != 200:
            raise HttpError(response.status_code, url)
        with mozfile.NamedTemporaryFile() as tmpfile:
            for data in response.iter_content(4096):
                tmpfile.write(data)

            tmpfile.seek(0)
            os.makedirs(os.path.dirname(destination), exist_ok=True)
            self._transform_single_file_to_destination(tmpfile, destination)
