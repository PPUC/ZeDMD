import subprocess
from pathlib import Path


def find_uf2conv(env):
    # try to find uf2conv.py inside the RP2040 arduino framework package.
    p = env.PioPlatform()
    pkg_dir = p.get_package_dir("framework-arduinopico")
    if pkg_dir:
        conv = Path(pkg_dir) / "tools" / "uf2conv.py"
        if conv.exists():
            return conv
    return None


def fs2uf2_action(source, target, env):
    current_env = env["PIOENV"]

    print(f"=== [fs2uf2:{current_env}] building LittleFS image... ===")

    # build firmware and fs for this specific environment
    env.Execute(f"pio run -e {current_env}")
    env.Execute(f"pio run -e {current_env} -t buildfs")

    build_dir = Path(env["PROJECT_BUILD_DIR"] + "/" + current_env)
    firmware_uf2 = build_dir / "firmware.uf2"
    fs_bin = build_dir / "littlefs.bin"
    fs_data_uf2 = build_dir / "littlefs_data.uf2"

    if not firmware_uf2.exists():
        raise FileNotFoundError(f"[fs2uf2:{current_env}] firmware.uf2 not found at {firmware_uf2}")
    if not fs_bin.exists():
        raise FileNotFoundError(f"[fs2uf2:{current_env}] littlefs.bin not found at {fs_bin}")

    print(f"=== [fs2uf2:{current_env}] converting littlefs.bin to littlefs_data.uf2 ===")

    # get fs offset
    fs_offset = 0x10000000 + int(env["PICO_FLASH_LENGTH"])

    # get family
    family = "0xe48bff59" if env["BOARD_MCU"] == "rp2350" else "0xe48bff56"

    # create little fs data uf2
    uf2conv = find_uf2conv(env)
    if not uf2conv:
        raise FileNotFoundError("❌ uf2conv.py not found in Arduino Pico framework")

    cmd = [
        env["PYTHONEXE"],  # use PlatformIO's python
        str(uf2conv),
        "--convert",
        "--base", hex(fs_offset),
        "--family", str(family),
        str(fs_bin),
        "--output", str(fs_data_uf2)
    ]

    print(f"[fs2uf2:{current_env}] running command:\n ", " ".join(str(c) for c in cmd))
    subprocess.check_call(cmd)
    print(f"=== [fs2uf2:{current_env}] ✅ created uf2: {fs_data_uf2} ===")


# run only if the current command line targets include 'fs2uf2'
if "fs2uf2" in COMMAND_LINE_TARGETS:
    fs2uf2_alias = Alias("fs2uf2", None, fs2uf2_action)
    AlwaysBuild(fs2uf2_alias)
