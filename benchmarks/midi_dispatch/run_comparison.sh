#!/usr/bin/env bash
set -euo pipefail

repo=$(git rev-parse --show-toplevel)
base_ref=${1:-a713070d}
current_ref=${2:-HEAD}
output_dir=${3:-"$repo/benchmark-results/midi-dispatch"}
iterations=${ITERATIONS:-50}
work_root=$(mktemp -d "${TMPDIR:-/tmp}/sfizz-midi-benchmark.XXXXXX")
base_src="$work_root/base-src"
current_src="$work_root/current-src"

cleanup() {
    git -C "$repo" worktree remove --force "$base_src" >/dev/null 2>&1 || true
    git -C "$repo" worktree remove --force "$current_src" >/dev/null 2>&1 || true
    rm -rf "$work_root"
}
trap cleanup EXIT

mkdir -p "$output_dir"
git -C "$repo" worktree add --detach "$base_src" "$base_ref"
git -C "$repo" worktree add --detach "$current_src" "$current_ref"
git -C "$base_src" submodule update --init --recursive
git -C "$current_src" submodule update --init --recursive

# The harness deliberately uses only APIs available at the SMPL-93 baseline.
# Copy the exact current harness into the baseline worktree, then expose its
# standalone CMake option without otherwise modifying baseline engine code.
mkdir -p "$base_src/benchmarks/midi_dispatch"
cp "$current_src/benchmarks/midi_dispatch/main.cpp" \
   "$current_src/benchmarks/midi_dispatch/CMakeLists.txt" \
   "$base_src/benchmarks/midi_dispatch/"
python3 - "$base_src/CMakeLists.txt" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
text = path.read_text()
if 'SFIZZ_MIDI_DISPATCH_BENCHMARK' not in text:
    text = text.replace(
        'option_ex(SFIZZ_BENCHMARKS          "Enable benchmarks build" OFF)',
        'option_ex(SFIZZ_BENCHMARKS          "Enable benchmarks build" OFF)\n'
        'option_ex(SFIZZ_MIDI_DISPATCH_BENCHMARK "Enable standalone MIDI dispatch benchmark" OFF)')
    text = text.replace(
        'if(SFIZZ_TESTS)\n',
        'if(SFIZZ_MIDI_DISPATCH_BENCHMARK)\n'
        '    add_subdirectory(benchmarks/midi_dispatch)\n'
        'endif()\n\nif(SFIZZ_TESTS)\n')
path.write_text(text)
PY

build_and_run() {
    local source=$1
    local label=$2
    local build="$work_root/$label-build"
    cmake -S "$source" -B "$build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS=-include\ cstdint \
        -DSFIZZ_MIDI_DISPATCH_BENCHMARK=ON \
        -DSFIZZ_TESTS=OFF -DSFIZZ_JACK=OFF -DSFIZZ_RENDER=OFF \
        -DSFIZZ_SHARED=OFF >"$work_root/$label-build.log"
    cmake --build "$build" --target sfizz_midi_dispatch_benchmark -j2 \
        >>"$work_root/$label-build.log"
    "$build/library/bin/sfizz_midi_dispatch_benchmark" \
        --iterations "$iterations" >"$output_dir/$label.csv"
}

# Run sequentially so the two timing sets do not compete for CPU resources.
build_and_run "$base_src" base
build_and_run "$current_src" current

{
    printf 'base_ref=%s\n' "$(git -C "$base_src" rev-parse HEAD)"
    printf 'current_ref=%s\n' "$(git -C "$current_src" rev-parse HEAD)"
    printf 'iterations=%s\n' "$iterations"
    printf 'compiler=%s\n' "$(c++ --version | head -1)"
    printf 'kernel=%s\n' "$(uname -srmo)"
    printf 'cpu=%s\n' "$(awk -F: '/model name/{gsub(/^ /, "", $2); print $2; exit}' /proc/cpuinfo)"
} >"$output_dir/environment.txt"

python3 - "$output_dir/base.csv" "$output_dir/current.csv" \
    >"$output_dir/comparison.csv" <<'PY'
import csv, sys
with open(sys.argv[1], newline='') as f:
    base = list(csv.DictReader(f))
with open(sys.argv[2], newline='') as f:
    current = list(csv.DictReader(f))
print('scenario,regions,controllers,base_mean_ns,current_mean_ns,current_over_base,base_allocations,current_allocations')
for old, new in zip(base, current):
    assert (old['scenario'], old['regions'], old['controllers']) == (new['scenario'], new['regions'], new['controllers'])
    ratio = int(new['mean_ns']) / int(old['mean_ns'])
    print(','.join((old['scenario'], old['regions'], old['controllers'],
        old['mean_ns'], new['mean_ns'], f'{ratio:.4f}',
        old['first_block_allocations'], new['first_block_allocations'])))
PY

printf 'Wrote comparison to %s\n' "$output_dir"
