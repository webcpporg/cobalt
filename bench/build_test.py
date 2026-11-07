# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Controls against mislabeled optimization, LTO and instrumented measurements."""
import unittest
import tempfile
from pathlib import Path
from unittest.mock import patch
import build
import process


class EvidenceTests(unittest.TestCase):
    def test_unclassified_warning_cannot_be_reported_as_qualified(self):
        profile = dict(target="wasip3", exceptions="on", threading="single")
        diagnostic = "wasm-ld: warning: function signature mismatch: cabi_realloc"
        self.assertEqual(
            build.audit_warnings(diagnostic, profile, True)[0]["kind"],
            "sdk34-wasip3-lto-cabi-realloc-diagnostic",
        )
        with self.assertRaisesRegex(ValueError, "Unclassified"):
            build.audit_warnings(diagnostic, profile, False)
        with self.assertRaisesRegex(ValueError, "Unclassified"):
            build.audit_warnings("compiler: warning: another defect", profile, True)

    def test_profile_rejects_changed_exception_thread_or_suspension_flags(self):
        profile = dict(target="jspi", exceptions="on", threading="multi")
        compile_command = [
            "em++",
            "-fwasm-exceptions",
            "-pthread",
            "-c",
            "-o",
            "/bench/asio-workload.o",
            "workload.cpp",
        ]
        link = [
            "em++",
            "-fwasm-exceptions",
            "-pthread",
            "-sJSPI",
            "-o",
            "/bench/asio_workload",
            "input.o",
        ]
        build.audit_profile([compile_command], [link], profile)
        for flag in ("-fwasm-exceptions", "-pthread"):
            with self.assertRaises(ValueError):
                build.audit_profile([[w for w in compile_command if w != flag]], [link], profile)
        with self.assertRaisesRegex(ValueError, "suspension"):
            build.audit_profile(
                [compile_command], [[w for w in link if w != "-sJSPI"] + ["-sASYNCIFY"]], profile
            )

    def test_cooperative_profile_requires_its_sdk_sysroot(self):
        profile = dict(target="wasip3", exceptions="off", threading="multi")
        command = [
            "clang++",
            "--target=wasm32-wasip3",
            "-fno-exceptions",
            "-pthread",
            "-c",
            "-o",
            "/bench/asio-workload.o",
            "workload.cpp",
        ]
        with self.assertRaisesRegex(ValueError, "cooperative"):
            build.audit_profile([command], [], profile)

    def test_implementation_include_changes_source_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "libs/asio/include/webcpp/asio/detail/scheduler.ipp"
            source.parent.mkdir(parents=True)
            source.write_text("old implementation")
            with patch.object(build, "ROOT", root):
                before = build.source_identity()
                source.write_text("corrected implementation")
                after = build.source_identity()
            self.assertNotEqual(before, after)

    def test_last_optimization_wins(self):
        compile_line = (
            '"clang++" -O3 -Oz -DNDEBUG -c -o "/bench/asio-workload-object.o" "workload.cpp"'
        )
        links = "\n".join(
            '"clang++" -Oz -o "/bench/' + name + '" "input.o"' for name in build.PROGRAMS
        )
        build.audit_commands(compile_line + "\n" + links, "Oz", False)
        with self.assertRaisesRegex(ValueError, "optimization"):
            build.audit_commands(compile_line + "\n" + links, "O3", False)

    def test_wrong_library_object_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "macro"):
            build.audit_commands(
                '"clang++" -O3 -DNDEBUG -c -o "/bench/cobalt-workload-object.o" "workload.cpp"',
                "O3",
                False,
            )

    def test_lto_label_requires_actual_flag(self):
        with self.assertRaisesRegex(ValueError, "LTO"):
            build.audit_commands(
                '"clang++" -O3 -DNDEBUG -c -o "/bench/asio-workload-object.o" "workload.cpp"',
                "O3",
                True,
            )

    def test_unavailable_is_not_zero(self):
        data = dict(
            operations=100,
            checksum=5050,
            cpp_allocations=0,
            cobalt_frame_requests=0,
            linear_memory_before=0,
            linear_memory_after=0,
        )
        result = process.normalize({"measurement": data}, "asio", "workload", "native")
        self.assertIsNone(result["measurement"]["cpp_allocations"])
        self.assertIsNone(result["measurement"]["linear_memory_after"])
        self.assertIsNone(result["measurement"]["cobalt_frame_requests"])

    def test_checksum_is_required(self):
        with self.assertRaisesRegex(ValueError, "checksum"):
            process.normalize(
                {"measurement": dict(operations=100, checksum=0)}, "asio", "workload", "native"
            )


if __name__ == "__main__":
    unittest.main()
