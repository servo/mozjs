# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import hashlib
import http.client
import json
import os
import platform
import shutil
import signal
import subprocess
import tempfile
import time
import zipfile
from pathlib import Path
from urllib.parse import unquote

from mozlog import get_proxy_logger

from .symbolicationRequest import SymbolicationRequest
from .symFileManager import SymFileManager

LOG = get_proxy_logger("profiler")
BREAKPAD_SYMBOL_SERVER = "https://symbols.mozilla.org/"
SYMBOL_SERVER_TIMEOUT = 60  # seconds
SAMPLY_WAIT_TIMEOUT = 60  # seconds

from io import BytesIO as sio
from urllib.request import urlopen


class SymbolError(Exception):
    pass


class OSXSymbolDumper:
    def __init__(self):
        self.dump_syms_bin = os.path.join(os.path.dirname(__file__), "dump_syms_mac")
        if not os.path.exists(self.dump_syms_bin):
            raise SymbolError("No dump_syms_mac binary in this directory")

    def store_symbols(
        self, lib_path, expected_breakpad_id, output_filename_without_extension
    ):
        """
        Returns the filename at which the .sym file was created, or None if no
        symbols were dumped.
        """
        output_filename = output_filename_without_extension + ".sym"

        def get_archs(filename):
            """
            Find the list of architectures present in a Mach-O file.
            """
            return (
                subprocess
                .Popen(["lipo", "-info", filename], stdout=subprocess.PIPE)
                .communicate()[0]
                .split(b":")[2]
                .strip()
                .split()
            )

        def process_file(arch):
            proc = subprocess.Popen(
                [self.dump_syms_bin, "-a", arch, lib_path],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            stdout, stderr = proc.communicate()
            if proc.returncode != 0:
                return None

            module = stdout.splitlines()[0]
            bits = module.split(b" ", 4)
            if len(bits) != 5:
                return None
            _, platform, cpu_arch, actual_breakpad_id, debug_file = bits

            if str(actual_breakpad_id, "utf-8") != expected_breakpad_id:
                return None

            with open(output_filename, "wb") as f:
                f.write(stdout)
            return output_filename

        for arch in get_archs(lib_path):
            result = process_file(arch)
            if result is not None:
                return result
        return None


class LinuxSymbolDumper:
    def __init__(self):
        self.nm = shutil.which("nm")
        if not self.nm:
            raise SymbolError("Could not find nm, necessary for symbol dumping")

    def store_symbols(self, lib_path, breakpad_id, output_filename_without_extension):
        """
        Returns the filename at which the .sym file was created, or None if no
        symbols were dumped.
        """
        output_filename = output_filename_without_extension + ".nmsym"

        proc = subprocess.Popen(
            [self.nm, "--demangle", lib_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        stdout, stderr = proc.communicate()

        if proc.returncode != 0:
            return

        with open(output_filename, "wb") as f:
            f.write(stdout)

            # Append nm -D output to the file. On Linux, most system libraries
            # have no "normal" symbols, but they have "dynamic" symbols, which
            # nm -D shows.
            proc = subprocess.Popen(
                [self.nm, "--demangle", "-D", lib_path],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            stdout, stderr = proc.communicate()
            if proc.returncode == 0:
                f.write(stdout)
        return output_filename


class ProfileSymbolicator:
    """This class orchestrates symbolication for a Gecko profile.

    It can be used by multiple pieces of testing infrastructure that generate Gecko
    performance profiles.

    Args:
        options (obj): See SymFileManager for details on these options.
    """

    def __init__(self, options):
        self.options = options
        self.sym_file_manager = SymFileManager(self.options)
        self.symbol_dumper = self.get_symbol_dumper()

    def get_symbol_dumper(self):
        try:
            if platform.system() == "Darwin":
                return OSXSymbolDumper()
            elif platform.system() == "Linux":
                return LinuxSymbolDumper()
        except SymbolError:
            return None

    def integrate_symbol_zip_from_url(self, symbol_zip_url):
        if self.have_integrated(symbol_zip_url):
            return
        LOG.info(f"Retrieving symbol zip from {symbol_zip_url}...")
        try:
            io = urlopen(symbol_zip_url, None, 30)
            with zipfile.ZipFile(sio(io.read())) as zf:
                self.integrate_symbol_zip(zf)
            self._create_file_if_not_exists(self._marker_file(symbol_zip_url))
        except (OSError, http.client.IncompleteRead):
            LOG.info("Symbol zip request failed.")

    def integrate_symbol_zip_from_file(self, filename):
        if self.have_integrated(filename):
            return
        with open(filename, "rb") as f:
            with zipfile.ZipFile(f) as zf:
                self.integrate_symbol_zip(zf)
        self._create_file_if_not_exists(self._marker_file(filename))

    def _create_file_if_not_exists(self, filename):
        try:
            os.makedirs(os.path.dirname(filename))
        except OSError:
            pass
        try:
            open(filename, "a").close()
        except OSError:
            pass

    def integrate_symbol_zip(self, symbol_zip_file):
        symbol_zip_file.extractall(self.options["symbolPaths"]["FIREFOX"])

    def _marker_file(self, symbol_zip_url):
        marker_dir = os.path.join(self.options["symbolPaths"]["FIREFOX"], ".markers")
        return os.path.join(
            marker_dir, hashlib.sha1(symbol_zip_url.encode("utf-8")).hexdigest()
        )

    def have_integrated(self, symbol_zip_url):
        return os.path.isfile(self._marker_file(symbol_zip_url))

    def get_unknown_modules_in_profile(self, profile_json):
        if "libs" not in profile_json:
            return []
        shared_libraries = profile_json["libs"]
        memoryMap = []
        for lib in shared_libraries:
            memoryMap.append([lib["debugName"], lib["breakpadId"]])

        rawRequest = {
            "stacks": [[]],
            "memoryMap": memoryMap,
            "version": 4,
            "symbolSources": ["FIREFOX", "WINDOWS"],
        }
        request = SymbolicationRequest(self.sym_file_manager, rawRequest)
        if not request.isValidRequest:
            return []
        request.Symbolicate(0)  # This sets request.knownModules

        unknown_modules = []
        for i, lib in enumerate(shared_libraries):
            if not request.knownModules[i]:
                unknown_modules.append(lib)
        return unknown_modules

    def dump_and_integrate_missing_symbols(self, profile_json, symbol_zip_path):
        if not self.symbol_dumper:
            return

        unknown_modules = self.get_unknown_modules_in_profile(profile_json)
        if not unknown_modules:
            return

        # We integrate the dumped symbols by dumping them directly into our
        # symbol directory.
        output_dir = self.options["symbolPaths"]["FIREFOX"]

        # Additionally, we add all dumped symbol files to the missingsymbols
        # zip file.
        with zipfile.ZipFile(symbol_zip_path, "a", zipfile.ZIP_DEFLATED) as zf:
            for lib in unknown_modules:
                self.dump_and_integrate_symbols_for_lib(lib, output_dir, zf)

    def dump_and_integrate_symbols_for_lib(self, lib, output_dir, zip):
        name = lib["debugName"]
        expected_name_without_extension = os.path.join(name, lib["breakpadId"], name)
        for extension in [".sym", ".nmsym"]:
            expected_name = expected_name_without_extension + extension
            if expected_name in zip.namelist():
                # No need to dump the symbols again if we already have it in
                # the missingsymbols zip file from a previous run.
                zip.extract(expected_name, output_dir)
                return

        lib_path = lib["path"]
        if not os.path.exists(lib_path):
            return

        output_filename_without_extension = os.path.join(
            output_dir, expected_name_without_extension
        )
        store_path = os.path.dirname(output_filename_without_extension)
        if not os.path.exists(store_path):
            os.makedirs(store_path)

        # Dump the symbols.
        sym_file = self.symbol_dumper.store_symbols(
            lib_path, lib["breakpadId"], output_filename_without_extension
        )
        if sym_file:
            rootlen = len(os.path.join(output_dir, "_")) - 1
            output_filename = sym_file[rootlen:]
            if output_filename not in zip.namelist():
                zip.write(sym_file, output_filename)

    def _symbolicate_profile_fallback(self, profile_json):

        if "libs" not in profile_json:
            return

        shared_libraries = profile_json["libs"]
        addresses = self._find_addresses(profile_json)
        symbols_to_resolve = self._assign_symbols_to_libraries(
            addresses, shared_libraries
        )
        symbolication_table = self._resolve_symbols(symbols_to_resolve)
        self._substitute_symbols(profile_json, symbolication_table)

        for process in profile_json["processes"]:
            self._symbolicate_profile_fallback(process)

    def _validate_symbolication_deps(self, paths_to_validate):
        for dep_path in paths_to_validate:
            if not dep_path.exists():
                LOG.warning(f"{dep_path} does not exist.")
                return False
        return True

    def symbolicate_profile(self, profile_json):

        # Check if running in CI
        if "MOZ_AUTOMATION" in os.environ:
            moz_fetch = os.environ["MOZ_FETCHES_DIR"]
            profiler_edit_path = Path(
                moz_fetch, "profiler-node-tools", "profiler-edit.js"
            )
            if platform.system() == "Windows":
                samply_path = Path(moz_fetch, "samply", "samply.exe")
                node_path = Path(moz_fetch, "node", "node.exe")
            else:
                samply_path = Path(moz_fetch, "samply", "samply")
                node_path = Path(moz_fetch, "node", "bin", "node")

            # Check if symbolication dependencies are available

            if not self._validate_symbolication_deps([
                profiler_edit_path,
                samply_path,
                node_path,
            ]):
                LOG.info(
                    "Symbolication dependencies not available, using fallback symbolication."
                )
                self._symbolicate_profile_fallback(profile_json)
                return

            try:
                breakpad_symbol_dir = self.options["symbolPaths"]["FIREFOX"]

                with tempfile.TemporaryDirectory() as work_dir:
                    unsym_profile = Path(work_dir, "unsym_profile.json")
                    unsym_profile.write_text(
                        json.dumps(profile_json, ensure_ascii=False), encoding="utf-8"
                    )
                    sym_profile = Path(work_dir) / "sym_profile.json"

                    # Load unsymbolicated profile with samply
                    samply_process = subprocess.Popen(
                        [
                            samply_path,
                            "load",
                            str(unsym_profile),
                            "--no-open",
                            "--breakpad-symbol-dir",
                            str(breakpad_symbol_dir),
                            "--breakpad-symbol-server",
                            BREAKPAD_SYMBOL_SERVER,
                        ],
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                        text=True,
                    )

                    # Tail output for timeout seconds to obtain symbol server url
                    server_url = ""
                    start = time.time()
                    with samply_process.stdout:
                        for line in iter(samply_process.stdout.readline, ""):
                            if line.startswith("http"):
                                url = unquote(line)
                                server_url = str(url.split("symbolServer=", 1)[-1])
                                break
                            timeout = time.time() - start
                            if timeout > SYMBOL_SERVER_TIMEOUT:
                                raise TimeoutError(
                                    f"Server timed out after exceeding {SYMBOL_SERVER_TIMEOUT} seconds. Time elapsed : {timeout} seconds."
                                )

                    with subprocess.Popen(
                        [
                            node_path,
                            str(profiler_edit_path),
                            "-i",
                            str(unsym_profile),
                            "-o",
                            str(sym_profile),
                            "--symbolicate-with-server",
                            server_url,
                        ],
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                        text=True,
                        bufsize=1,
                    ) as profiler_edit_process:
                        # Stream and forward to self.info()
                        for line in profiler_edit_process.stdout:
                            LOG.info(f"profiler-edit {line.strip()}")

                    # Terminate samply server
                    if platform.system() == "Windows":
                        samply_process.terminate()
                    else:
                        samply_process.send_signal(signal.SIGINT)  # ctrl-c shutdown

                    samply_process.wait(timeout=SAMPLY_WAIT_TIMEOUT)

                    # Load profile json into memory and mutate profile
                    with sym_profile.open("r", encoding="utf-8") as f:
                        sym = json.load(f)

                    profile_json.clear()
                    profile_json.update(sym)

            except Exception:
                LOG.critical("Profile symbolication failed.", exc_info=True)
                LOG.info("Attempting fallback symbolication.")
                self._symbolicate_profile_fallback(profile_json)

        # Local symbolication using fallback symbolication
        else:
            LOG.info("Running locally - using fallback symbolication.")
            self._symbolicate_profile_fallback(profile_json)

    def _find_addresses(self, profile_json):
        addresses = set()
        for thread in profile_json["threads"]:
            if isinstance(thread, str):
                continue
            for s in thread["stringTable"]:
                if s[0:2] == "0x":
                    addresses.add(s)
        return addresses

    def _substitute_symbols(self, profile_json, symbolication_table):
        for thread in profile_json["threads"]:
            if isinstance(thread, str):
                continue
            for i, s in enumerate(thread["stringTable"]):
                thread["stringTable"][i] = symbolication_table.get(s, s)

    def _get_containing_library(self, address, libs):
        left = 0
        right = len(libs) - 1
        while left <= right:
            mid = (left + right) // 2
            if address >= libs[mid]["end"]:
                left = mid + 1
            elif address < libs[mid]["start"]:
                right = mid - 1
            else:
                return libs[mid]
        return None

    def _assign_symbols_to_libraries(self, addresses, shared_libraries):
        libs_with_symbols = {}
        for address in addresses:
            lib = self._get_containing_library(int(address, 0), shared_libraries)
            if not lib:
                continue
            if lib["start"] not in libs_with_symbols:
                libs_with_symbols[lib["start"]] = {"library": lib, "symbols": set()}
            libs_with_symbols[lib["start"]]["symbols"].add(address)
        # pylint: disable=W1656
        return libs_with_symbols.values()

    def _resolve_symbols(self, symbols_to_resolve):
        memoryMap = []
        processedStack = []
        all_symbols = []
        for moduleIndex, library_with_symbols in enumerate(symbols_to_resolve):
            lib = library_with_symbols["library"]
            symbols = library_with_symbols["symbols"]
            memoryMap.append([lib["debugName"], lib["breakpadId"]])
            all_symbols += symbols
            for symbol in symbols:
                processedStack.append([moduleIndex, int(symbol, 0) - lib["start"]])

        rawRequest = {
            "stacks": [processedStack],
            "memoryMap": memoryMap,
            "version": 4,
            "symbolSources": ["FIREFOX", "WINDOWS"],
        }
        request = SymbolicationRequest(self.sym_file_manager, rawRequest)
        if not request.isValidRequest:
            return {}
        symbolicated_stack = request.Symbolicate(0)
        return dict(zip(all_symbols, symbolicated_stack))
