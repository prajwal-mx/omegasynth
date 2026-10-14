#!/bin/bash
# Official SYNTCOMP runner script for OmegaSynth
# Usage:
#   ./syntcomp_run.sh <benchmark_file.ehoa>           (Realizability Track: PGreal)
#   ./syntcomp_run.sh <benchmark_file.ehoa> --aiger   (Synthesis Track: PGsynt)

if [ -z "$1" ]; then
    echo "Usage: $0 <benchmark_file.hoa> [--aiger]" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ "$2" = "--aiger" ]; then
    exec "$SCRIPT_DIR/omegasynth" hoa "$1" --zielonka --preprocess --syntcomp --emit-aiger
else
    exec "$SCRIPT_DIR/omegasynth" hoa "$1" --zielonka --preprocess --syntcomp
fi
