#!/bin/bash
# Builds the VST3, AU and Standalone app (universal: Apple silicon + Intel).
# Requires: Xcode (with command line tools) and CMake 3.22+ (brew install cmake).
set -euo pipefail
cd "$(dirname "$0")"
cmake -B build -G Xcode
cmake --build build --config Release
echo
echo "Built. With COPY_PLUGIN_AFTER_BUILD the plugins were copied to:"
echo "  ~/Library/Audio/Plug-Ins/VST3/DT2 Emulator.vst3"
echo "  ~/Library/Audio/Plug-Ins/Components/DT2 Emulator.component"
echo "Standalone app: build/DT2Emulator_artefacts/Release/Standalone/"
