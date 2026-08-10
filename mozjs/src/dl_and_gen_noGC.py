#!/usr/bin/env python3

import gzip
from urllib.request import urlretrieve

with open("mozjs-sys/etc/COMMIT", "r") as f:
    commit = f.read().strip()

print(f"Commit: {commit}")

url = f"https://github.com/servo/mozjs/releases/download/mozjs-source-{commit}"

urlretrieve(f"{url}/gcFunctions.txt.gz", "target/gcFunctions.txt.gz")
urlretrieve(f"{url}/allFunctions.txt.gz", "target/allFunctions.txt.gz")

gc_functions = set()

with gzip.open("target/gcFunctions.txt.gz", "rt") as f:
    for line in f:
        if line.startswith("GC Function: "):
            stripped_line = line.removeprefix("GC Function: ").strip()
            gc_functions.add(stripped_line)

with gzip.open(
    "target/noGC.txt.gz", "wt"
) as out_file:  # 'wt' for text mode writing to a gzipped file
    with gzip.open("target/allFunctions.txt.gz", "rt") as f:
        for line in f:
            line_s = line.strip()
            if (
                "mozilla::dom" not in line_s
                and "mozilla::net" not in line_s
                and line_s not in gc_functions
            ):
                out_file.write(line)  # Write the line to the new file
