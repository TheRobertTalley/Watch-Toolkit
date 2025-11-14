#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
import sys
from os.path import join
import os
import json
import re

from readprops import readProps

Import("env")
platform = env.PioPlatform()


def esp32_create_combined_bin(source, target, env):
    # this sub is borrowed from ESPEasy build toolchain. It's licensed under GPL V3
    # https://github.com/letscontrolit/ESPEasy/blob/mega/tools/pio/post_esp32.py
    print("Generating combined binary for serial flashing")

    app_offset = 0x10000

    new_file_name = env.subst("$BUILD_DIR/${PROGNAME}.factory.bin")
    sections = env.subst(env.get("FLASH_EXTRA_IMAGES"))
    firmware_name = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    chip = env.get("BOARD_MCU")
    flash_size = env.BoardConfig().get("upload.flash_size")
    flash_freq = env.BoardConfig().get("build.f_flash", "40m")
    flash_freq = flash_freq.replace("000000L", "m")
    flash_mode = env.BoardConfig().get("build.flash_mode", "dio")
    memory_type = env.BoardConfig().get("build.arduino.memory_type", "qio_qspi")
    if flash_mode == "qio" or flash_mode == "qout":
        flash_mode = "dio"
    if memory_type == "opi_opi" or memory_type == "opi_qspi":
        flash_mode = "dout"
    cmd = [
        "--chip",
        chip,
        "merge_bin",
        "-o",
        new_file_name,
        "--flash_mode",
        flash_mode,
        "--flash_freq",
        flash_freq,
        "--flash_size",
        flash_size,
    ]

    print("    Offset | File")
    for section in sections:
        sect_adr, sect_file = section.split(" ", 1)
        print(f" -  {sect_adr} | {sect_file}")
        cmd += [sect_adr, sect_file]

    print(f" - {hex(app_offset)} | {firmware_name}")
    cmd += [hex(app_offset), firmware_name]

    print("Using esptool.py arguments: %s" % " ".join(cmd))

    esptool.main(cmd)


if platform.name == "espressif32":
    sys.path.append(join(platform.get_package_dir("tool-esptoolpy")))
    import esptool

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", esp32_create_combined_bin)

    esp32_kind = env.GetProjectOption("custom_esp32_kind")
    if esp32_kind == "esp32":
        # Free up some IRAM by removing auxiliary SPI flash chip drivers.
        # Wrapped stub symbols are defined in src/platform/esp32/iram-quirk.c.
        env.Append(
            LINKFLAGS=[
                "-Wl,--wrap=esp_flash_chip_gd",
                "-Wl,--wrap=esp_flash_chip_issi",
                "-Wl,--wrap=esp_flash_chip_winbond",
            ]
        )
    else:
        # For newer ESP32 targets, using newlib nano works better.
        env.Append(LINKFLAGS=["--specs=nano.specs", "-u", "_printf_float"])

if platform.name == "nordicnrf52":
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex",
                      env.VerboseAction(f"\"{sys.executable}\" ./bin/uf2conv.py $BUILD_DIR/firmware.hex -c -f 0xADA52840 -o $BUILD_DIR/firmware.uf2",
                                        "Generating UF2 file"))

Import("projenv")

prefsLoc = projenv["PROJECT_DIR"] + "/version.properties"
verObj = readProps(prefsLoc)
print("Using meshtastic platformio-custom.py, firmware version " + verObj["long"] + " on " + env.get("PIOENV"))

jsonLoc = env["PROJECT_DIR"] + "/userPrefs.jsonc"
with open(jsonLoc) as f:
    jsonStr = re.sub(r"//.*", "", f.read(), flags=re.MULTILINE)
    # Allow control characters (e.g. literal newlines) inside JSON strings so multi-line values are accepted.
    userPrefs = json.loads(jsonStr, strict=False)


def _should_use_header(val: str) -> bool:
    """Values that are very large or include C-style initializers work better from a header than command-line defines."""
    return val.startswith("{") or "\n" in val or len(val) > 1024


def _format_define_value(val: str) -> str:
    """Preserve multi-line brace initializers by adding line continuations."""
    cleaned = val.replace("\r", "")
    lines = cleaned.split("\n")
    if len(lines) == 1:
        return lines[0]
    formatted = []
    for idx, line in enumerate(lines):
        suffix = " \\" if idx < len(lines) - 1 else ""
        formatted.append(f"{line.rstrip()}{suffix}")
    return "\n".join(formatted)


header_needed = False
header_defines = []
pref_flags = []
# Pre-process the userPrefs
for pref in userPrefs:
    value = userPrefs[pref]
    if isinstance(value, str):
        if _should_use_header(value):
            header_needed = True
            if value.startswith("{"):
                header_defines.append(f"#define {pref} {_format_define_value(value)}")
            else:
                header_defines.append(f"#define {pref} {env.StringifyMacro(value)}")
            continue
        if value.lstrip("-").replace(".", "").isdigit():
            pref_flags.append("-D" + pref + "=" + value)
        elif value == "true" or value == "false":
            pref_flags.append("-D" + pref + "=" + value)
        elif value.startswith("meshtastic_"):
            pref_flags.append("-D" + pref + "=" + value)
        else:
            pref_flags.append("-D" + pref + "=" + env.StringifyMacro(value) + "")
    else:
        pref_flags.append("-D" + pref + "=" + str(value))

if header_needed:
    header_path = join(env.subst("$BUILD_DIR"), "userPrefs_autogen.h")
    os.makedirs(join(env.subst("$BUILD_DIR")), exist_ok=True)
    with open(header_path, "w", encoding="utf-8") as header_file:
        header_file.write("// Auto-generated from userPrefs.jsonc. Do not edit.\n")
        header_file.write("#pragma once\n\n")
        header_file.write("\n".join(header_defines))
        header_file.write("\n")
    projenv.Append(CCFLAGS=["-include", header_path])

# General options that are passed to the C and C++ compilers
flags = [
        "-DAPP_VERSION=" + verObj["long"],
        "-DAPP_VERSION_SHORT=" + verObj["short"],
        "-DAPP_ENV=" + env.get("PIOENV"),
    ] + pref_flags

print ("Using flags:")
for flag in flags:
    print(flag)
    
projenv.Append(
    CCFLAGS=flags,
)

for lb in env.GetLibBuilders():
    if lb.name == "meshtastic-device-ui":
        lb.env.Append(CPPDEFINES=[("APP_VERSION", verObj["long"])])
        break
