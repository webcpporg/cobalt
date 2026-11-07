# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Check every optimized payload before reporting sizes; these are not timings."""
import argparse
import json
from pathlib import Path

import experiment


def cases(receipts):
    prepared = experiment.cases(receipts, {name: 32 for name in experiment.WORKLOADS}, smoke=True)[
        "process"
    ]
    result = []
    for case in prepared:
        if case["mode"] == "memory":
            continue
        case["id"] = case["build_id"] + "-" + case["id"]
        result.append(case)
    if len(result) != 2176 or len({case["id"] for case in result}) != len(result):
        raise ValueError("Expected all 136 builds, two libraries, baseline and seven workloads")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--builds", type=Path, required=True)
    args = parser.parse_args()
    receipts = [
        json.loads(path.read_text())
        for path in sorted(args.builds.glob("*.json"))
        if not path.name.endswith(".sources.json")
    ]
    if len(receipts) != 136 or any(row["status"] != "passed" for row in receipts):
        raise ValueError("Payload qualification requires all successful builds")
    experiment.run_process(cases(receipts), args.builds / "qualification.jsonl")


if __name__ == "__main__":
    main()
