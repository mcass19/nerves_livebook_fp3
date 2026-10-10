#!/bin/sh
# Cross-compiles gpubench and cpupeak against a nerves_system_fp3 artifact.
# Usage: build.sh <nerves_system_fp3 artifact directory>
set -e
A=${1:?usage: build.sh ~/Projects/nerves_system_fp3/.nerves/artifacts/nerves_system_fp3-portable-0.3.0}
CC="$A/host/bin/aarch64-nerves-linux-gnu-gcc --sysroot=$A/staging"
cd "$(dirname "$0")"
$CC -O2 -Wall -Wno-unused-parameter -o gpubench gpubench.c -lEGL -lGLESv2 -lgbm -lm
$CC -O2 -Wall -o cpupeak cpupeak.c -lpthread
