// SPDX-License-Identifier: BSD-2-Clause

#include "sfizz.h"
#include "catch2/catch.hpp"
#include <cmath>
#include <memory>
#include <vector>

namespace {
constexpr int blockSize = 256;
constexpr float sampleRate = 48000;

struct PitchSynth {
    std::unique_ptr<sfizz_synth_t, decltype(&sfizz_free)> synth {
        sfizz_create_synth(), sfizz_free };

    PitchSynth()
    {
        sfizz_set_sample_rate(synth.get(), sampleRate);
        sfizz_set_samples_per_block(synth.get(), blockSize);
        sfizz_set_mpe_enabled(synth.get(), true);
        sfizz_set_mpe_pitch_bend_range(synth.get(), 2, 48);
        REQUIRE(sfizz_load_string(synth.get(), "mpe-release-pitch.sfz",
            "<region> sample=*sine ampeg_release=4"));
    }

    std::vector<float> render(int blocks = 100)
    {
        float left[blockSize], right[blockSize];
        float* channels[] = { left, right };
        std::vector<float> output;
        for (int i = 0; i < blocks; ++i) {
            sfizz_render_block(synth.get(), channels, 2, blockSize);
            output.insert(output.end(), left, left + blockSize);
        }
        return output;
    }

    void bend(int channel, float semitones, int delay = 0)
    {
        sfizz_send_hd_pitch_wheel_channel(synth.get(), delay, channel,
            semitones / (channel == 0 ? 2 : 48));
    }

    void start()
    {
        bend(1, 0.5f);
        sfizz_send_note_on_channel(synth.get(), 0, 1, 69, 100);
    }
};

double frequency(const std::vector<float>& audio)
{
    std::vector<double> crossings;
    // Let bend smoothing settle before measuring the second half.
    for (size_t i = audio.size() / 2 + 1; i < audio.size(); ++i) {
        if (audio[i - 1] <= 0 && audio[i] > 0)
            crossings.push_back(i - 1 - audio[i - 1]
                / double(audio[i] - audio[i - 1]));
    }
    REQUIRE(crossings.size() > 2);
    return sampleRate * (crossings.size() - 1)
        / (crossings.back() - crossings.front());
}

double a4(float semitones)
{
    return 440 * std::pow(2.0, semitones / 12);
}
} // namespace

TEST_CASE("[MPE release pitch] Note Off retains Member bend in rendered audio")
{
    PitchSynth f;
    const int pedal = GENERATE(0, 64, 66);
    if (pedal == 64)
        sfizz_send_cc_channel(f.synth.get(), 0, 0, pedal, 127);
    f.start();
    REQUIRE(frequency(f.render()) == Approx(a4(0.5f)).margin(0.01));
    if (pedal == 66) {
        sfizz_send_cc_channel(f.synth.get(), 0, 0, pedal, 127);
        f.render(1);
    }
    sfizz_send_note_off_channel(f.synth.get(), 0, 1, 69, 0);
    CHECK(frequency(f.render()) == Approx(a4(0.5f)).margin(0.01));

    // The Member may be reset for its next note; the old tail stays tuned.
    f.bend(1, -24);
    CHECK(frequency(f.render()) == Approx(a4(0.5f)).margin(0.01));
    f.bend(0, 1);
    CHECK(frequency(f.render()) == Approx(a4(1.5f)).margin(0.01));
    if (pedal) {
        sfizz_send_cc_channel(f.synth.get(), 0, 0, pedal, 0);
        CHECK(frequency(f.render()) == Approx(a4(1.5f)).margin(0.01));
    }
}

TEST_CASE("[MPE release pitch] Note Off preserves bend timing within its block")
{
    PitchSynth released, held;
    for (auto* f : { &released, &held }) {
        sfizz_send_cc_channel(f->synth.get(), 0, 0, 64, 127);
        f->start();
        f->render();
        f->bend(1, 2, 64);
        f->bend(1, -1, 128);
    }
    sfizz_send_note_off_channel(released.synth.get(), 192, 1, 69, 0);
    // Recycling the logical-note slot must not replace the old pitch timeline.
    released.bend(1, 24, 200);
    sfizz_send_note_on_channel(released.synth.get(), 200, 1, 81, 1);
    // Compare only samples before the new voice starts.
    const auto actual = released.render(1);
    const auto expected = held.render(1);
    for (int i = 0; i < 200; ++i)
        REQUIRE(actual[i] == Approx(expected[i]).margin(1e-6));
}

TEST_CASE("[MPE release pitch] Reused Member and voice keep independent pitch")
{
    PitchSynth f;
    REQUIRE(sfizz_load_string(f.synth.get(), "mpe-reuse-pitch.sfz",
        "<region> sample=*sine key=69 ampeg_release=4 "
        "<region> sample=*sine key=81 volume=-144"));
    f.start();
    f.render();
    sfizz_send_note_off_channel(f.synth.get(), 0, 1, 69, 0);
    f.bend(1, -24, 1);
    sfizz_send_note_on_channel(f.synth.get(), 1, 1, 81, 100);
    CHECK(frequency(f.render()) == Approx(a4(0.5f)).margin(0.01));
    f.bend(1, 24);
    CHECK(frequency(f.render()) == Approx(a4(0.5f)).margin(0.01));
    // Reusing a voice must clear the previous note's frozen bend.
    sfizz_all_sound_off(f.synth.get());
    f.bend(1, -1);
    sfizz_send_note_on_channel(f.synth.get(), 0, 1, 69, 100);
    CHECK(frequency(f.render()) == Approx(a4(-1)).margin(0.01));
}
