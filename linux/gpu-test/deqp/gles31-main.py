#!/usr/bin/env python3
"""Collect GLES31 main cases from a VK-GL-CTS checkout's Android manifest.

Usage: gles31-main.py CTS_SOURCE OUTPUT
The dated main lists are incremental; taking only the newest loses cases.
This selects case names, without importing Android's window configurations.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    base = args.source / "android/cts/main"
    manifest = base / "mustpass.xml"
    package = ET.parse(manifest).find("TestPackage[@name='dEQP-GLES31']")
    if package is None:
        parser.error("manifest has no dEQP-GLES31 package")
    cases, files = {}, []
    for config in package.findall("Configuration"):
        filename = config.get("caseListFile", "")
        if not re.fullmatch(r"gles31-main-\d{4}-\d{2}-\d{2}\.txt", filename):
            continue
        data = (base / filename).read_bytes()
        lines = [line.strip() for line in data.decode().splitlines() if line.strip()]
        if any(not line.startswith("dEQP-GLES31.") for line in lines):
            parser.error(f"unexpected case name in {filename}")
        cases.update(dict.fromkeys(lines))
        files.append(dict(file=filename, sha256=hashlib.sha256(data).hexdigest(), cases=len(lines)))
    if not files or not cases:
        parser.error("no dated GLES31 main cases found")
    data = ("\n".join(cases) + "\n").encode()
    args.output.write_bytes(data)
    print(json.dumps(dict(source="android/cts/main/mustpass.xml", files=files,
                          union_cases=len(cases), list_sha256=hashlib.sha256(data).hexdigest()), indent=2))


if __name__ == "__main__":
    main()
