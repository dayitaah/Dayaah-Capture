#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p out
for name in test_disco_effect test_disco_secret test_latency_estimator test_mode_selection test_overlay_model test_overlay_profile test_window_behavior test_video_effects_model; do
    g++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread "$name.cpp" -o "out/$name"
    "out/$name"
done
