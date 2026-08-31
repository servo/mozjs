# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from setuptools import find_packages, setup

VERSION = "0.1"

setup(
    author="Mozilla Foundation",
    author_email="dev-builds@lists.mozilla.org",
    name="mozcmakeparser",
    description="Mozilla Custom CMake parser.",
    license="MPL 2.0",
    packages=find_packages(),
    version=VERSION,
    install_requires=[
        "pyparsing==2.4.7",
        "colorama",
    ],
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Topic :: Software Development :: Build Tools",
        "License :: OSI Approved :: Mozilla Public License 2.0 (MPL 2.0)",
        "Programming Language :: Python :: 2.7",
        "Programming Language :: Python :: Implementation :: CPython",
    ],
    keywords="mozilla build",
)
