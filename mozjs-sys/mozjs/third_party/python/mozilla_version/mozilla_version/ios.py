"""Defines the characteristics of an iOS version number."""

import re

import attr

from mozilla_version.parser import positive_int_or_none

from .version import ShipItVersion


@attr.s(frozen=True, eq=False, hash=True)
class MobileIosVersion(ShipItVersion):
    """
    Class representing an iOS version number.

    iOS version numbers are a bit different in that they don't have a patch number
    but they have a beta one.
    """

    beta_number = attr.ib(type=int, converter=positive_int_or_none, default=None)

    _VALID_ENOUGH_VERSION_PATTERN = re.compile(
        r"""
        ^(?P<major_number>\d+)
        \.(?P<minor_number>\d+)
        (\.(?P<patch_number>\d+))?
        (b(?P<beta_number>\d+))?$""",
        re.VERBOSE,
    )

    _OPTIONAL_NUMBERS = (
        "patch_number",
        "beta_number",
    )
    _ALL_NUMBERS = ShipItVersion._MANDATORY_NUMBERS + _OPTIONAL_NUMBERS

    def __str__(self):
        """
        Format the version as a string.

        Because iOS is different, the format is "major.minor(.beta)".
        """
        version = f"{self.major_number}.{self.minor_number}"

        if self.patch_number:
            version += f".{self.patch_number}"

        if self.beta_number is not None:
            version += f"b{self.beta_number}"

        return version

    def _create_bump_kwargs(self, field):
        """Create a version bump for the required field."""
        kwargs = super()._create_bump_kwargs(field)

        # If we get a bump request for anything but the beta number, remove it
        if field != "beta_number":
            del kwargs["beta_number"]

        # Prevent patch_number from being set to 0
        # This happens when bumping minor_number on a version like 2.0
        if kwargs.get("patch_number") == 0:
            del kwargs["patch_number"]

        return kwargs

    @property
    def is_beta(self):
        """Returns true if the version is considered a beta one."""
        return self.beta_number is not None

    @property
    def is_release_candidate(self):
        """
        Returns true if the version is a release candidate.

        For iOS versions, this is always false.
        """
        return False

    def _compare(self, other):
        """Compare this release with another."""
        if isinstance(other, str):
            other = MobileIosVersion.parse(other)
        if not isinstance(other, MobileIosVersion):
            raise ValueError(f'Cannot compare "{other}", type not supported!')

        difference = super()._compare(other)
        if difference != 0:
            return difference

        return self._substract_other_number_from_this_number(other, "beta_number")

    def _get_all_error_messages_for_attributes(self):
        error_messages = super()._get_all_error_messages_for_attributes()

        error_messages.extend(
            [
                pattern_message
                for condition, pattern_message in (
                    (
                        self.beta_number is not None and self.patch_number is not None,
                        "Beta number and patch number cannot be both defined",
                    ),
                    (
                        self.patch_number == 0,
                        "The patch number should be absent instead of being 0",
                    ),
                )
                if condition
            ]
        )

        return error_messages
