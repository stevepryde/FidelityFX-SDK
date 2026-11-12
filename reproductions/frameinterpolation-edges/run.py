#!/usr/bin/env python3
"""Compile AMD's game-field source and run the standalone Vulkan reproduction."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--sdk", type=Path, required=True, help="FidelityFX SDK checkout (1.1.4 or current layout)")
p.add_argument("--vulkan-sdk", type=Path, help="Vulkan SDK platform directory; optional with system Vulkan on Linux")
p.add_argument("--dxc", help="DXC executable; otherwise use Vulkan SDK bin or PATH")
p.add_argument("--cxx", help="C++ compiler; defaults to cl on Windows, clang++ on macOS, c++ on Linux")
p.add_argument("--build-dir", type=Path, default=Path(__file__).resolve().parent / "build")
a = p.parse_args()
source = Path(__file__).resolve().parent
sdk = a.sdk.resolve()
legacy = sdk / "sdk/include/FidelityFX/gpu"
if legacy.is_dir():
    includes = [legacy]
else:
    includes = [sdk / "Kits/FidelityFX/framegeneration/fsr3/include/gpu", sdk / "Kits/FidelityFX/api/internal/gpu"]
header = includes[0] / "frameinterpolation/ffx_frameinterpolation_game_motion_vector_field.h"
print("AMD source:", header, flush=True)
print("Source SHA-256:", hashlib.sha256(header.read_bytes()).hexdigest(), flush=True)
build = a.build_dir.resolve()
build.mkdir(parents=True, exist_ok=True)
vulkan = a.vulkan_sdk.resolve() if a.vulkan_sdk else None
candidates = ([vulkan / "bin/dxc", vulkan / "bin/dxc-3.7", vulkan / "Bin/dxc.exe"] if vulkan else [])
dxc = a.dxc or next((str(f) for f in candidates if f.is_file()), None) or shutil.which("dxc")
if not dxc:
    p.error("supply --dxc or --vulkan-sdk containing DXC")
spirv = build / "repro.spv"
command = [dxc, "-spirv", "-fspv-target-env=vulkan1.1", "-T", "cs_6_2", "-E", "main", "-HV", "2021"]
for path in includes:
    command += ["-I", str(path)]
subprocess.run(command + [str(source / "repro.hlsl"), "-Fo", str(spirv)], check=True)
if sys.platform == "win32":
    if not vulkan:
        p.error("Windows needs --vulkan-sdk and a Visual Studio developer shell")
    executable = build / "fi-edge-repro.exe"
    command = [a.cxx or "cl", "/nologo", "/std:c++17", "/EHsc", "/O2", f"/I{vulkan / 'Include'}", str(source / "repro.cpp"), f"/Fe:{executable}", "/link", str(vulkan / "Lib/vulkan-1.lib")]
else:
    executable = build / "fi-edge-repro"
    command = [a.cxx or ("clang++" if sys.platform == "darwin" else "c++"), "-std=c++17", "-O2", str(source / "repro.cpp"), "-o", str(executable)]
    if vulkan:
        command += ["-I", str(vulkan / "include"), "-L", str(vulkan / "lib"), f"-Wl,-rpath,{vulkan / 'lib'}"]
    if sys.platform == "darwin":
        if not vulkan:
            p.error("macOS needs --vulkan-sdk with MoltenVK")
        command += ["-lMoltenVK"]
    else:
        command += ["-lvulkan"]
subprocess.run(command, cwd=build, check=True)
result = subprocess.run([str(executable), str(spirv)], cwd=build, capture_output=True, text=True)
print(result.stdout, end="")
print(result.stderr, file=sys.stderr, end="")
(build / "result.txt").write_text(result.stdout + result.stderr)
result.check_returncode()
