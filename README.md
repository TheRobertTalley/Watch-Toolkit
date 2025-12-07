Watch Toolkit

A Meshtastic-based ESP32/ESP32-S3 firmware toolkit for real-world field utilities

Watch Toolkit is a modular extension of the Meshtastic firmware ecosystem, built specifically for ESP32-based watch and handheld devices. It adds practical field-ready tools on top of the Meshtastic stack, including network diagnostics, on-device calculators, radio utilities, and specialized mission functions.

This project reflects how I build embedded systems: hardware-aware, testable, modular, and designed for repeatable deployment across multiple board variants.

What This Toolkit Adds Beyond Meshtastic
Network & Radio Tools

On-device mesh signal diagnostics

Packet inspection utilities

Node scanning and link-quality tools

Visual mesh-health indicators for quick field assessment

Operational Utility Tools

Explosives Breacher Calculator (scaled down for onsite math)

Distance and angle helpers

Quick-reference utilities (timers, waypoint markers, compass, etc.)

Modular plugin system for future tools

Wearable Enhancements

Watch-optimized UI with LVGL

Battery monitoring widgets

Touch + button hybrid input handling

Power-aware display control for long runtime

Engineering / Developer Features

Clean board definitions for multiple ESP32-S3 watch form factors

Reproducible PlatformIO builds

CI, static analysis, and fuzzing for communication parsers

Devcontainer for consistent cross-platform development

Quick Start
git clone https://github.com/TheRobertTalley/Watch-Toolkit
cd Watch-Toolkit
pio run -e <board>
pio run -e <board> -t upload


Boards and variants live under boards/ and variants/, including pin maps and display definitions.

Why This Project Exists

I built Watch Toolkit because I needed a Meshtastic-capable device that could do more than messaging. Real deployments need tools you can access immediately on your wrist—diagnostics, quick math, timers, angles, and mission-support utilities that do not depend on a phone.

Instead of hacking one-offs, I built a structured firmware base that:

Extends Meshtastic cleanly

Runs reliably on multiple ESP32-S3 watch platforms

Includes real features for real work, not demo code

Uses proper testing, reproducible builds, and clean hardware abstraction

This project is the best reflection of how I operate as an embedded engineer: take a proven base (Meshtastic) and extend it with stable, mission-useful features.

Highlights for Reviewers & Hiring Managers

This repo demonstrates that I:

1. Understand Embedded Systems at a Practical Level

I integrate displays, IMUs, audio, sensors, I2C peripherals, LVGL, and mesh radios in a production-style codebase.

2. Work Directly on Networking/Mesh Firmware

Modifying and extending Meshtastic requires understanding protocol flow, memory constraints, timing, message parsing, and RF realities.

3. Build Tools With Real Operational Purpose

The explosives breacher calculator, timers, mesh diagnostics, and UI modules show that I build firmware that solves real problems, not academic demos.

4. Use Professional Engineering Practices

The repo includes:

Semgrep static analysis

ClusterFuzzLite for parser fuzzing

GitHub Actions CI

Docker/DevContainer support for reproducible development

Modular board/variant layering

This is how I actually work day-to-day when building embedded systems.

5. Combine Firmware + Python + Tooling

Host utilities, MicroPython tools, and integration scripts show that I work across both firmware and supporting automation environments.

Toolkit Structure
Watch-Toolkit/
│
├── meshtastic-mods/     # Extensions, handlers, and tools built on Meshtastic core
├── tools/               # Field utilities (calculators, diagnostics, timers, etc.)
├── variants/            # Board-specific pinouts, LVGL configs, display drivers
├── boards/              # Board definitions for PlatformIO
├── examples/            # Minimal demo apps and hardware test utilities
├── docker/              # Reproducible build environment
├── tests/               # Unit tests and fuzzing harness
└── platformio.ini       # Firmware build config

Planned Expansions

Additional breaching and engineering calculators

Extended mesh-health visualization tools

Hardware-in-the-loop testing rig

More watch/handheld variants

Modular radio-diagnostics plugins
