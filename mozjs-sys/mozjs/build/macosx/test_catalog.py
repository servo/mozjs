# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import unittest
from io import StringIO
from unittest.mock import patch

import catalog
import mozunit

DIST_XML_DIRECT_TITLE = b"""<?xml version="1.0"?>
<installer-script>
<title>Command Line Tools for Xcode</title>
</installer-script>"""

DIST_XML_LOCALIZED_TITLE = b"""<?xml version="1.0"?>
<installer-script>
<strings>"TOOL_TITLE" = "Localized SDK Title";</strings>
<title>TOOL_TITLE</title>
</installer-script>"""

PRODUCT_INFO_XML = b"""<?xml version="1.0"?>
<installer-script>
<pkg-ref id="com.apple.pkg.CLTools_Executables">CLTools_Executables.pkg</pkg-ref>
<pkg-ref id="com.apple.pkg.CLTools_SDK">CLTools_SDK.pkg</pkg-ref>
</installer-script>"""


class TestShowProducts(unittest.TestCase):
    SERVER_METADATA = {
        "localization": {"English": {"title": "Command Line Tools"}},
        "CFBundleShortVersionString": "14.2",
    }

    def _make_product_with_server_metadata(self, metadata_url_fragment="MOS_SDK"):
        return {
            "001-11111": {
                "ServerMetadataURL": "http://example.com/meta.plist",
                "Packages": [
                    {
                        "MetadataURL": f"http://example.com/{metadata_url_fragment}/meta.plist"
                    }
                ],
            }
        }

    def test_server_metadata_url_prints_key_title_version(self):
        products = self._make_product_with_server_metadata()
        with patch("catalog.get_plist_at", return_value=self.SERVER_METADATA):
            with patch("sys.stdout", new_callable=StringIO) as mock_out:
                catalog.show_products(products)
        self.assertEqual(
            mock_out.getvalue().strip(), "001-11111 Command Line Tools 14.2"
        )

    def test_filter_matches_package_metadata_url(self):
        products = self._make_product_with_server_metadata("MOS_SDK")
        with patch("catalog.get_plist_at", return_value=self.SERVER_METADATA):
            with patch("sys.stdout", new_callable=StringIO) as mock_out:
                catalog.show_products(products, filter="MOS_SDK")
        self.assertEqual(
            mock_out.getvalue().strip(), "001-11111 Command Line Tools 14.2"
        )

    def test_filter_excludes_non_matching_products(self):
        products = self._make_product_with_server_metadata("OTHER")
        with patch("catalog.get_plist_at", return_value=self.SERVER_METADATA):
            with patch("sys.stdout", new_callable=StringIO) as mock_out:
                catalog.show_products(products, filter="MOS_SDK")
        self.assertEqual(mock_out.getvalue(), "")

    def test_filter_excludes_product_with_no_packages(self):
        products = {
            "001-22222": {
                "ServerMetadataURL": "http://example.com/meta.plist",
                "Packages": [],
            }
        }
        with patch("catalog.get_plist_at", return_value=self.SERVER_METADATA):
            with patch("sys.stdout", new_callable=StringIO) as mock_out:
                catalog.show_products(products, filter="MOS_SDK")
        self.assertEqual(mock_out.getvalue(), "")

    def test_distributions_direct_title(self):
        products = {
            "002-33333": {
                "Distributions": {"English": "http://example.com/dist.xml"},
                "Packages": [],
            }
        }
        with patch("catalog.get_content_at", return_value=DIST_XML_DIRECT_TITLE):
            with patch("sys.stdout", new_callable=StringIO) as mock_out:
                catalog.show_products(products)
        self.assertEqual(
            mock_out.getvalue().strip(), "002-33333 Command Line Tools for Xcode"
        )

    def test_distributions_localized_title(self):
        products = {
            "002-44444": {
                "Distributions": {"English": "http://example.com/dist.xml"},
                "Packages": [],
            }
        }
        with patch("catalog.get_content_at", return_value=DIST_XML_LOCALIZED_TITLE):
            with patch("sys.stdout", new_callable=StringIO) as mock_out:
                catalog.show_products(products)
        self.assertEqual(mock_out.getvalue().strip(), "002-44444 Localized SDK Title")

    def test_no_metadata_produces_no_output(self):
        products = {"003-55555": {"Packages": []}}
        with patch("sys.stdout", new_callable=StringIO) as mock_out:
            catalog.show_products(products)
        self.assertEqual(mock_out.getvalue(), "")


class TestShowProductInfo(unittest.TestCase):
    PRODUCT = {
        "Distributions": {"English": "http://example.com/dist.xml"},
        "Packages": [
            {
                "URL": "http://example.com/CLTools_Executables.pkg",
                "Digest": "abc123",
                "Size": 1024,
            },
            {
                "URL": "http://example.com/CLTools_SDK.pkg",
                "Digest": "def456",
                "Size": 2048,
            },
        ],
    }

    def test_lists_all_package_ids_and_urls(self):
        with patch("catalog.get_content_at", return_value=PRODUCT_INFO_XML):
            with patch("sys.stdout", new_callable=StringIO) as mock_out:
                catalog.show_product_info(self.PRODUCT)
        lines = mock_out.getvalue().splitlines()
        self.assertIn(
            "com.apple.pkg.CLTools_Executables http://example.com/CLTools_Executables.pkg",
            lines,
        )
        self.assertIn(
            "com.apple.pkg.CLTools_SDK http://example.com/CLTools_SDK.pkg",
            lines,
        )

    def test_package_id_filter_calls_show_package_content(self):
        with patch("catalog.get_content_at", return_value=PRODUCT_INFO_XML):
            with patch("catalog.show_package_content") as mock_show:
                catalog.show_product_info(self.PRODUCT, "com.apple.pkg.CLTools_SDK")
        mock_show.assert_called_once_with(
            "http://example.com/CLTools_SDK.pkg", "def456", 2048
        )

    def test_package_id_filter_excludes_other_packages(self):
        with patch("catalog.get_content_at", return_value=PRODUCT_INFO_XML):
            with patch("catalog.show_package_content") as mock_show:
                catalog.show_product_info(
                    self.PRODUCT, "com.apple.pkg.CLTools_Executables"
                )
        mock_show.assert_called_once_with(
            "http://example.com/CLTools_Executables.pkg", "abc123", 1024
        )


if __name__ == "__main__":
    mozunit.main()
