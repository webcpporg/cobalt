# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Reject false completeness and preserve missing measurement semantics."""
import copy
import unittest
from typing import Any
import analyze


class AnalysisTests(unittest.TestCase):
    def fixture(self) -> dict[str, Any]:
        return dict(
            id="case",
            program="program",
            profile={"target": "native"},
            library="asio",
            host="native",
            build_id="native-single-on-O3-lto-off",
            mode="workload",
            workload="post",
            repeat=0,
            args=["post", "10", "2"],
            artifact_files=[],
            source_manifest="hash",
            status="passed",
            measurement={"workload": "post", "operations": 10, "checksum": 55},
        )

    def test_missing_sample_is_not_success(self):
        with self.assertRaisesRegex(ValueError, "Missing"):
            analyze.complete([self.fixture()], [])

    def test_wrong_workload_count_is_rejected_even_with_valid_checksum(self):
        expected = self.fixture()
        actual = copy.deepcopy(expected)
        actual["measurement"].update(operations=20, checksum=210)
        with self.assertRaisesRegex(ValueError, "frozen case"):
            analyze.complete([expected], [actual])

    def test_mixed_binary_is_rejected(self):
        expected = self.fixture()
        actual = copy.deepcopy(expected)
        actual["program"] = "other"
        with self.assertRaisesRegex(ValueError, "identity"):
            analyze.complete([expected], [actual])

    def test_reproducible_statistics(self):
        result = analyze.summary(range(1, 31))
        self.assertEqual(result["median"], 15.5)
        self.assertEqual(result["p95"], 29)
        self.assertEqual(result, analyze.summary(range(1, 31)))
        with self.assertRaisesRegex(ValueError, "Invalid"):
            analyze.summary([float("nan")])

    def test_runtime_must_match_selected_build_receipt(self):
        case = self.fixture()
        case["build_id"] = "native-single-on-O3-lto-off"
        receipt: dict[str, Any] = dict(
            id=case["build_id"],
            profile=case["profile"],
            source_manifest="different-source",
            optimization="O3",
            lto="off",
            artifacts={"asio_workload": dict(program=case["program"], files=[])},
        )
        with self.assertRaisesRegex(ValueError, "build receipt"):
            analyze.bind_builds([case], [receipt])
        receipt["source_manifest"] = case["source_manifest"]
        analyze.bind_builds([case], [receipt])
        receipt["artifacts"]["asio_workload"]["files"] = [{"sha256": "other"}]
        with self.assertRaisesRegex(ValueError, "build receipt"):
            analyze.bind_builds([case], [receipt])

    def test_deleting_entire_profile_is_not_complete(self):
        receipt: dict[str, Any] = dict(
            id="native-single-on-O3-lto-off",
            profile=dict(
                id="native-single-on", target="native", threading="single", exceptions="on"
            ),
            optimization="O3",
            lto="off",
            source_manifest="hash",
            artifacts={
                lib + "_" + mode: dict(program=lib + "_" + mode, files=[])
                for lib in ("asio", "cobalt")
                for mode in ("baseline", "workload", "memory")
            },
        )
        counts = {workload: 256 for workload in analyze.experiment.WORKLOADS}
        with self.assertRaisesRegex(ValueError, "canonical"):
            analyze.canonical_cases([], [receipt], counts, "process")
        second = copy.deepcopy(receipt)
        second["id"] = "wasip2-single-on-O3-lto-off"
        second["profile"].update(id="wasip2-single-on", target="wasip2")
        cases = analyze.experiment.cases([receipt, second], counts)["process"]
        analyze.canonical_cases(cases, [second, receipt], counts, "process")


if __name__ == "__main__":
    unittest.main()
