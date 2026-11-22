"""Build the standalone SA_Harvester plugin with 32-bit MinGW-w64."""

import os
from pathlib import Path
import shutil
import subprocess


def main():
    root = Path(__file__).resolve().parent
    env = os.environ.copy()
    toolchain = env.get("MOE_MINGW")
    if not toolchain and env.get("LOCALAPPDATA"):
        portable = Path(env["LOCALAPPDATA"]) / "MOEE-BuildTools" / "mingw32" / "bin"
        if (portable / "g++.exe").is_file():
            toolchain = str(portable)
    if toolchain:
        env["PATH"] = toolchain + os.pathsep + env.get("PATH", "")

    compiler = shutil.which("g++", path=toolchain or env.get("PATH"))
    if not compiler:
        raise SystemExit("Set MOE_MINGW to the i686 MinGW-w64 bin directory.")
    target = subprocess.check_output([compiler, "-dumpmachine"], env=env, text=True).strip()
    if target != "i686-w64-mingw32":
        raise SystemExit(f"Expected i686-w64-mingw32, got {target}")

    dist = root / "dist"
    dist.mkdir(exist_ok=True)
    variants = (
        ("SA_Harvester.dll", []),
        ("SA_Harvester_NoLog.dll", ["-DSA_HARVESTER_NO_LOG"]),
    )
    for filename, variant_flags in variants:
        dll = dist / filename
        subprocess.run(
            [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-fno-exceptions", "-fno-rtti",
             "-fno-asynchronous-unwind-tables", "-fno-builtin", "-shared", *variant_flags,
             "-o", str(dll), "src/Harvester.cpp", "src/Engine.cpp", "src/SyringeHooks.cpp",
             "-nostdlib", "-lkernel32", "-luser32", "-lgcc", "-s",
             "-Wl,--no-insert-timestamp", "-Wl,--entry,_DllMain@12"],
            cwd=root, env=env, check=True,
        )
        print(f"Built {dll}")


if __name__ == "__main__":
    main()
