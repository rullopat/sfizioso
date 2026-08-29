// SPDX-License-Identifier: BSD-2-Clause

#include "sfizz/AudioBuffer.h"
#include "sfizz/Synth.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <string>
#include <vector>

namespace {

thread_local bool countAllocations = false;
thread_local uint64_t allocationCount = 0;

void* allocate(std::size_t size)
{
    if (countAllocations)
        ++allocationCount;
    if (void* ptr = std::malloc(size))
        return ptr;
    throw std::bad_alloc {};
}

std::string makeInstrument(int regions, int controllers)
{
    std::ostringstream sfz;
    sfz << "<global>";
    for (int cc = 0; cc < controllers; ++cc)
        sfz << " cutoff_oncc" << cc << "=100";
    for (int region = 0; region < regions; ++region)
        sfz << "\n<region> key=60 sample=*sine";
    return sfz.str();
}

enum class Traffic {
    Idle,
    Legacy,
    Mpe,
    OverlappingMpe,
    Dense,
};

const char* trafficName(Traffic traffic)
{
    switch (traffic) {
    case Traffic::Idle: return "idle";
    case Traffic::Legacy: return "legacy";
    case Traffic::Mpe: return "mpe";
    case Traffic::OverlappingMpe: return "overlapping_mpe";
    case Traffic::Dense: return "dense";
    }
    return "unknown";
}

void dispatchTraffic(sfz::Synth& synth, Traffic traffic)
{
    switch (traffic) {
    case Traffic::Idle:
        return;
    case Traffic::Legacy:
        synth.noteOn(0, 0, 60, 100);
        for (int event = 0; event < 64; ++event) {
            const int delay = event * 3;
            synth.pitchWheel(delay, 0, (event * 257) & 0x3fff);
            synth.channelAftertouch(delay, 0, event & 0x7f);
            synth.cc(delay, 0, event & 0x7f, event & 0x7f);
        }
        return;
    case Traffic::Mpe:
        for (int channel = 1; channel < 16; ++channel)
            synth.noteOn(channel, channel, 60, 100);
        for (int event = 0; event < 64; ++event) {
            const int channel = 1 + event % 15;
            const int delay = event * 3;
            synth.pitchWheel(delay, channel, (event * 257) & 0x3fff);
            synth.channelAftertouch(delay, channel, event & 0x7f);
            synth.cc(delay, channel, 74, event & 0x7f);
        }
        return;
    case Traffic::OverlappingMpe:
        for (int channel = 1; channel < 16; ++channel) {
            for (int overlap = 0; overlap < 4; ++overlap)
                synth.noteOn(overlap, channel, 60, 100);
        }
        for (int event = 0; event < 64; ++event) {
            const int channel = 1 + event % 15;
            synth.pitchWheel(event * 3, channel, (event * 257) & 0x3fff);
        }
        return;
    case Traffic::Dense:
        for (int channel = 1; channel < 16; ++channel)
            synth.noteOn(channel, channel, 60, 100);
        for (int event = 0; event < 512; ++event) {
            const int channel = 1 + event % 15;
            const int delay = event % 256;
            synth.cc(delay, channel, event & 0x7f, (event * 17) & 0x7f);
            synth.pitchWheel(delay, channel, (event * 257) & 0x3fff);
            synth.channelAftertouch(delay, channel, event & 0x7f);
        }
        return;
    }
}

bool runCase(Traffic traffic, int regions, int controllers, int iterations)
{
    sfz::Synth synth;
    synth.setSamplesPerBlock(256);
    synth.setNumVoices(128);
    synth.setMPEEnabled(
        traffic == Traffic::Mpe || traffic == Traffic::OverlappingMpe
        || traffic == Traffic::Dense);
    const std::string sfz = makeInstrument(regions, controllers);
    if (!synth.loadSfzString("midi-dispatch-benchmark.sfz", sfz)) {
        std::cerr << "could not load generated SFZ for "
                  << trafficName(traffic) << '\n';
        return false;
    }

    sfz::AudioBuffer<float> buffer { 2, 256 };
    uint64_t firstAllocations = 0;
    uint64_t totalNanoseconds = 0;
    uint64_t minimumNanoseconds = std::numeric_limits<uint64_t>::max();
    for (int iteration = 0; iteration < iterations; ++iteration) {
        buffer.clear();
        if (iteration == 0) {
            allocationCount = 0;
            countAllocations = true;
        }
        const auto started = std::chrono::steady_clock::now();
        dispatchTraffic(synth, traffic);
        synth.renderBlock(buffer);
        const auto finished = std::chrono::steady_clock::now();
        if (iteration == 0) {
            countAllocations = false;
            firstAllocations = allocationCount;
        }
        const uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                finished - started).count());
        totalNanoseconds += elapsed;
        minimumNanoseconds = std::min(minimumNanoseconds, elapsed);
        synth.allSoundOff();
    }

    std::cout << trafficName(traffic) << ',' << regions << ',' << controllers
              << ',' << iterations << ',' << totalNanoseconds / iterations
              << ',' << minimumNanoseconds << ',' << firstAllocations << '\n';
    return true;
}

} // namespace

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }

int main(int argc, char** argv)
{
    int iterations = 50;
    if (argc == 3 && std::string(argv[1]) == "--iterations")
        iterations = std::max(1, std::atoi(argv[2]));
    else if (argc != 1) {
        std::cerr << "usage: " << argv[0] << " [--iterations N]\n";
        return 2;
    }

    std::cout << "scenario,regions,controllers,iterations,mean_ns,min_ns,first_block_allocations\n";
    for (Traffic traffic : { Traffic::Idle, Traffic::Legacy, Traffic::Mpe }) {
        for (int regions : { 1, 100, 1000 }) {
            for (int controllers : { 1, 16, 128 }) {
                if (!runCase(traffic, regions, controllers, iterations))
                    return 1;
            }
        }
    }
    if (!runCase(Traffic::OverlappingMpe, 100, 16, iterations))
        return 1;
    if (!runCase(Traffic::Dense, 100, 128, iterations))
        return 1;
    return 0;
}
