#!/usr/bin/env python3
"""Reject empty, skipped or failed scene sanitizer test suites."""
import argparse
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


REQUIRED = frozenset({"CookedObjectSnapshotTest", "CookedPackageSnapshotTest", "SceneLoadCoordinatorTest",
                      "SceneRuntimeCameraTest", "SceneSubmissionProgressTest", "SceneInstanceTest",
                      "SceneResourceBundleTest", "SceneEnvironmentHandoffTest"})


def verify(path):
    root = ET.parse(path).getroot()
    if root.tag != "testsuites":
        raise ValueError("invalid Google Test report")
    suites = root.findall("testsuite")
    names = [suite.get("name") for suite in suites]
    if len(names) != len(set(names)) or set(names) != REQUIRED:
        raise ValueError("missing, repeated or unexpected scene test suite")
    for suite in suites:
        cases = suite.findall("testcase")
        if not cases or int(suite.get("tests", "-1")) != len(cases):
            raise ValueError("scene test suite has no complete test results")
        if any(int(suite.get(key, "0")) != 0 for key in ("failures", "errors", "skipped", "disabled")):
            raise ValueError("scene test suite contains failed, skipped or disabled tests")
        if any(case.get("status") != "run" or case.find("skipped") is not None or case.find("failure") is not None for case in cases):
            raise ValueError("scene test did not run successfully")
    return sum(len(suite.findall("testcase")) for suite in suites)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    print(f"Verified {verify(args.report)} executed scene sanitizer tests")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ET.ParseError, ValueError) as error:
        print(f"Scene sanitizer result validation failed: {error}", file=sys.stderr)
        sys.exit(1)
