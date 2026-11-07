# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Build isolated optimized experiments and audit the actual compiler inputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import shlex
import subprocess
import time
from typing import Any

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
PROGRAMS = tuple(
    f"{lib}_{mode}" for lib in ("asio", "cobalt") for mode in ("baseline", "workload", "memory")
)


def profiles():
    for target in ("native", "wasip2", "wasip3", "jspi", "asyncify"):
        for threading in ("single", "multi"):
            for eh in ("on", "off"):
                if target == "wasip2" and threading == "multi":
                    continue
                if target == "wasip3" and threading == "multi" and eh == "on":
                    continue
                yield dict(
                    id=f"{target}-{threading}-{eh}",
                    target=target,
                    threading=threading,
                    exceptions=eh,
                )


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_identity():
    files = {}
    configuration = [ROOT / "Jamroot", ROOT / ".local/review-b2", ROOT / ".local/user-config.jam"]
    configuration += [
        path
        for path in (ROOT / "tools").rglob("*")
        if path.suffix in (".jam", ".cpp", ".h", ".hpp", ".ipp", ".c", ".sh")
    ]
    for path in sorted(configuration):
        if path.is_file():
            files[str(path.relative_to(ROOT))] = digest(path)
    for name in ("asio", "cobalt", "beast", "config"):
        library = ROOT / "libs" / name
        paths = [library / "build.jam"]
        for directory in ("include", "src", "bench"):
            paths += list((library / directory).rglob("*"))
        for path in sorted(paths):
            if path.is_file() and "__pycache__" not in path.parts and "results" not in path.parts:
                if (
                    "include" in path.relative_to(library).parts
                    or "src" in path.relative_to(library).parts
                    or path.suffix
                    in (
                        ".h",
                        ".hpp",
                        ".ipp",
                        ".inl",
                        ".inc",
                        ".tcc",
                        ".cpp",
                        ".c",
                        ".cc",
                        ".cxx",
                        ".jam",
                        ".mjs",
                        ".js",
                    )
                    or path.name == "Jamfile"
                ):
                    files[str(path.relative_to(ROOT))] = digest(path)
    return files


def audit_commands(log, optimization, lto):
    commands = [
        shlex.split(line)
        for line in log.splitlines()
        if line.lstrip().startswith('"') and " -o " in line and ('.cpp"' in line or '.o"' in line)
    ]
    compiles = [cmd for cmd in commands if "-c" in cmd]
    if not compiles:
        raise ValueError("No executed compile command")
    for cmd in compiles:
        flags = [
            word for word in cmd if word in ("-O0", "-O1", "-O2", "-O3", "-Os", "-Oz", "-Ofast")
        ]
        if not flags or flags[-1] != "-" + optimization or "-DNDEBUG" not in cmd:
            raise ValueError("Effective optimization/NDEBUG differs: " + shlex.join(cmd))
        if lto != any(word.startswith("-flto") for word in cmd):
            raise ValueError("LTO compile flag differs")
        output = cmd[cmd.index("-o") + 1]
        if "/bench/" in output:
            cobalt = Path(output).name.startswith("cobalt-")
            memory = "-memory-" in Path(output).name
            if cobalt != ("-DWEBCPP_BENCH_COBALT" in cmd):
                raise ValueError("Cobalt object macro differs: " + output)
            if memory != ("-DWEBCPP_BENCH_TRACK_CPP_ALLOCATIONS" in cmd):
                raise ValueError("Instrumentation object macro differs: " + output)
    links = [cmd for cmd in commands if "-c" not in cmd and any("/bench/" in word for word in cmd)]
    if len(links) != len(PROGRAMS):
        raise ValueError(f"Expected six executed links, found {len(links)}")
    for cmd in links:
        if "-" + optimization not in cmd or lto != any(w.startswith("-flto") for w in cmd):
            raise ValueError("Link optimization/LTO differs")
    return compiles, links


def audit_profile(compiles, links, profile):
    target = profile["target"]
    browser = target in ("jspi", "asyncify")
    wasm = target != "native"
    eh_on = profile["exceptions"] == "on"
    expected_eh = "-fexceptions" if target == "asyncify" else "-fwasm-exceptions"
    commands = [
        (command, True) for command in compiles if "/bench/" in command[command.index("-o") + 1]
    ]
    commands += [(command, False) for command in links]
    for command, compiling in commands:
        eh_flags = [
            word
            for word in command
            if word in ("-fexceptions", "-fno-exceptions", "-fwasm-exceptions")
        ]
        effective_eh = eh_flags[-1] if eh_flags else None
        if (
            compiling
            and not eh_on
            and effective_eh != "-fno-exceptions"
            or eh_on
            and wasm
            and effective_eh != expected_eh
            or eh_on
            and effective_eh == "-fno-exceptions"
            or not eh_on
            and effective_eh in ("-fexceptions", "-fwasm-exceptions")
        ):
            raise ValueError("Effective exception mode differs from profile")
        if wasm and ("-pthread" in command) != (profile["threading"] == "multi"):
            raise ValueError("Effective thread mode differs from profile")
        if target.startswith("wasi") and "--target=wasm32-" + target not in command:
            raise ValueError("Effective WASI target differs from profile")
        if (
            target == "wasip3"
            and profile["threading"] == "multi"
            and not any(
                word.startswith("--sysroot=") and word.endswith("/experimental-coop-threads")
                for word in command
            )
        ):
            raise ValueError("Missing cooperative SDK sysroot")
        if browser and not compiling:
            settings = {}
            for word in command:
                if word in ("-sJSPI", "-sASYNCIFY") or word.startswith(("-sJSPI=", "-sASYNCIFY=")):
                    key, _, value = word.partition("=")
                    settings[key] = value not in ("0", "false")
            if settings.get("-sJSPI", False) != (target == "jspi") or settings.get(
                "-sASYNCIFY", False
            ) != (target == "asyncify"):
                raise ValueError("Effective suspension mode differs from profile")


def audit_warnings(log, profile, lto):
    warnings = []
    for line in log.splitlines():
        if "warning:" not in line:
            continue
        if profile["exceptions"] == "off" and "Cobalt without exceptions can abort" in line:
            kind = "documented-cobalt-issue-287"
        elif (
            profile["target"] == "native"
            and profile["exceptions"] == "off"
            and "'main_promise'" in line
            and "[-Wcoroutine-missing-unhandled-exception]" in line
        ):
            kind = "upstream-native-noeh-promise-diagnostic"
        elif (
            profile["target"] == "wasip3"
            and lto
            and "wasm-ld: warning: function signature mismatch: cabi_realloc" in line
        ):
            kind = "sdk34-wasip3-lto-cabi-realloc-diagnostic"
        else:
            raise ValueError("Unclassified build warning: " + line)
        warnings.append(dict(kind=kind, diagnostic=line))
    return warnings


def collect_artifacts(build):
    result = {}
    for name in PROGRAMS:
        choices = [
            path
            for path in build.rglob(name + "*")
            if path.is_file()
            and (path.name == name or path.suffix == ".js" or path.name == name + ".wasm")
        ]
        if len(choices) != 1:
            raise ValueError(f"{name}: ambiguous program: {choices}")
        main = choices[0]
        payload = [main]
        if main.suffix == ".js":
            payload += [main.with_suffix(".wasm")]
            payload += sorted(main.parent.glob(main.stem + "*.worker.*"))
        files: list[dict[str, Any]] = [
            dict(
                path=str(path.relative_to(ROOT)),
                bytes=path.stat().st_size,
                sha256=digest(path),
                kind=(
                    "javascript"
                    if path.suffix == ".js"
                    else "wasm" if path.suffix == ".wasm" else "native-executable"
                ),
            )
            for path in payload
        ]
        result[name] = dict(
            program=str(main.relative_to(ROOT)),
            files=files,
            total_bytes=sum(entry["bytes"] for entry in files),
        )
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--b2", default="b2")
    parser.add_argument("--user-config", type=Path)
    parser.add_argument("--profiles", nargs="*")
    parser.add_argument("--optimizations", nargs="*", default=["O2", "O3", "Os", "Oz"])
    parser.add_argument("--lto", nargs="*", choices=("off", "on"), default=["off", "on"])
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--seed", type=int, default=20261106)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument(
        "--compilation-context", choices=("preparation", "isolated"), default="preparation"
    )
    parser.add_argument("--stop-file", type=Path)
    args = parser.parse_args()
    output, build_root = args.output.resolve(), args.build_root.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sources = source_identity()
    source_hash = hashlib.sha256(json.dumps(sources, sort_keys=True).encode()).hexdigest()
    (output / (source_hash + ".sources.json")).write_text(json.dumps(sources, indent=2) + "\n")
    jobs = [
        (p, opt, lto)
        for p in profiles()
        if not args.profiles or p["id"] in args.profiles
        for opt in args.optimizations
        for lto in args.lto
    ]
    random.Random(args.seed).shuffle(jobs)
    env = dict(os.environ)
    for variable in ("CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH"):
        env.pop(variable, None)
    failed = []
    for profile, optimization, lto in jobs:
        if args.stop_file and args.stop_file.exists():
            print("STOP: requested between builds; completed receipts retained", flush=True)
            return 2
        name = profile["id"] + "-" + optimization + "-lto-" + lto
        receipt = output / (name + ".json")
        if args.resume and receipt.exists():
            old = json.loads(receipt.read_text())
            if (
                old["status"] == "passed"
                and old["source_manifest"] == source_hash + ".sources.json"
                and old["compilation_context"] == args.compilation_context
                and old.get("build_driver_sha256") == digest(Path(__file__))
            ):
                if all(
                    (ROOT / f["path"]).is_file() and digest(ROOT / f["path"]) == f["sha256"]
                    for app in old["artifacts"].values()
                    for f in app["files"]
                ):
                    print("REUSE " + name, flush=True)
                    continue
        build = build_root / name
        flags = "-" + optimization + (" -flto" if lto == "on" else "")
        command = [
            args.b2,
            "-a",
            "-d2",
            "-j" + str(args.jobs),
            "--build-dir=" + str(build),
            "variant=release",
            "threading=" + profile["threading"],
            "exception-handling=" + profile["exceptions"],
            "cxxflags=" + flags,
            "cflags=" + flags,
            "linkflags=" + flags,
        ]
        if args.user_config:
            command += ["--user-config=" + str(args.user_config.resolve())]
        target = profile["target"]
        if target in ("jspi", "asyncify"):
            command += ["toolset=emscripten", "webcpp-suspend=" + target]
        elif target != "native":
            command += ["toolset=clang-" + target]
        command += ["libs/cobalt/bench//" + program for program in PROGRAMS]
        record = dict(
            id=name,
            profile=profile,
            optimization=optimization,
            lto=lto,
            command=command,
            source_manifest=source_hash + ".sources.json",
            seed=args.seed,
            platform=platform.platform(),
            started=time.time(),
        )
        record["compilation_context"] = args.compilation_context
        record["build_driver_sha256"] = digest(Path(__file__))
        print("BUILD " + name, flush=True)
        log = output / (name + ".log")
        start = time.perf_counter()
        with log.open("w") as stream:
            stream.write(shlex.join(command) + "\n")
            stream.flush()
            run = subprocess.run(
                command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=900
            )
        record.update(
            build_seconds=time.perf_counter() - start,
            exit_code=run.returncode,
            log_sha256=digest(log),
            status="failed",
        )
        if source_identity() != sources:
            raise RuntimeError(
                "Source tree changed during measurement; restart with a fresh identity"
            )
        try:
            if run.returncode:
                raise ValueError("Build failed; see exact diagnostic in log")
            compiles, links = audit_commands(log.read_text(), optimization, lto == "on")
            audit_profile(compiles, links, profile)
            record["warnings"] = audit_warnings(log.read_text(), profile, lto == "on")
            objects = []
            for cmd in compiles:
                path = Path(cmd[cmd.index("-o") + 1])
                if not path.is_absolute():
                    path = ROOT / path
                magic = path.read_bytes()[:4].hex()
                bitcode = magic in ("4243c0de", "dec0170b")
                if bitcode != (lto == "on"):
                    raise ValueError("LTO object format differs: " + str(path))
                objects.append(
                    dict(path=str(path.relative_to(ROOT)), magic=magic, sha256=digest(path))
                )
            record.update(
                status="passed",
                artifacts=collect_artifacts(build),
                compile_commands=compiles,
                link_commands=links,
                objects=objects,
            )
        except (ValueError, OSError) as error:
            record["error"] = str(error)
            failed.append(name)
        receipt.write_text(json.dumps(record, indent=2) + "\n")
        print(f'{record["status"].upper()} {name}: {record["build_seconds"]:.2f}s', flush=True)
        if record["status"] != "passed":
            print(record["error"] + "; log: " + str(log), flush=True)
            return 1
    print("FAILED " + json.dumps(failed), flush=True)
    return bool(failed)


if __name__ == "__main__":
    raise SystemExit(main())
