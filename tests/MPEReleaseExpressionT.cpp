// SPDX-License-Identifier: BSD-2-Clause
#include "sfizz.h"
#include "sfizz/Synth.h"
#include "sfizz/Voice.h"
#include "sfizz/ModifierHelpers.h"
#include "catch2/catch.hpp"
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {
struct ExpressionSynth {
    static constexpr int block = 256;
    std::unique_ptr<sfizz_synth_t, decltype(&sfizz_free)> synth { sfizz_create_synth(), sfizz_free };
    int mapping;
    ExpressionSynth(int mapping) : mapping(mapping)
    {
        sfizz_set_sample_rate(synth.get(), 48000);
        sfizz_set_samples_per_block(synth.get(), block);
        sfizz_set_mpe_enabled(synth.get(), true);
        const char* mod = mapping == 0 ? "volume=-24 volume_oncc129=24"
            : mapping == 1 ? "cutoff=100 cutoff_chanaft=4800 fil_type=lpf_2p"
            : mapping == 2 ? "volume=-24 volume_oncc74=24"
            : "xfin_locc74=0 xfin_hicc74=127 xf_cccurve=gain";
        const std::string sfz = std::string("<region> sample=*sine key=69 ampeg_release=4 ")
            + mod + " <region> sample=*sine key=81 volume=-144";
        REQUIRE(sfizz_load_string(synth.get(), "mpe-expression.sfz", sfz.c_str()));
    }
    void expression(float value, int channel = 1, int delay = 0)
    {
        if (mapping >= 2)
            sfizz_send_hdcc_channel(synth.get(), delay, channel, 74, value);
        else
            sfizz_send_hd_channel_aftertouch_channel(synth.get(), delay, channel, value);
    }
    void pedal(int cc, int value = 127) { sfizz_send_cc_channel(synth.get(), 0, 0, cc, value); }
    void on(int key = 69, int delay = 0) { sfizz_send_note_on_channel(synth.get(), delay, 1, key, 100); }
    void off(int delay = 0) { sfizz_send_note_off_channel(synth.get(), delay, 1, 69, 0); }
    std::vector<float> render(int blocks = 100)
    {
        std::vector<float> out;
        float left[block], right[block]; float* channels[] { left, right };
        for (int i = 0; i < blocks; ++i) {
            sfizz_render_block(synth.get(), channels, 2, block);
            out.insert(out.end(), left, left + block);
        }
        return out;
    }
};
double rms(const std::vector<float>& audio)
{
    double sum = 0;
    for (size_t i = audio.size()/2; i < audio.size(); ++i) sum += audio[i]*audio[i];
    return std::sqrt(sum / (audio.size()-audio.size()/2));
}
}

TEST_CASE("[MPE release expression] Pressure and timbre retain the intended release shape")
{
    const int mapping = GENERATE(0, 1, 2, 3);
    const int pedal = GENERATE(0, 64, 66);
    ExpressionSynth actual(mapping), reference(mapping);
    for (auto* f : { &actual, &reference }) {
        f->expression(0.75f);
        if (pedal == 64) f->pedal(pedal);
        f->on();
    }
    actual.expression(0.25f, 0);
    reference.expression(0.75f, 0);
    // Both notes should follow their higher Member expression while held.
    CHECK(rms(actual.render()) == Approx(rms(reference.render())).margin(1e-6));
    if (pedal == 66) {
        actual.pedal(pedal); reference.pedal(pedal);
        actual.render(1); reference.render(1);
    }
    actual.off(); reference.off();
    CHECK(rms(actual.render()) == Approx(rms(reference.render())).margin(1e-6));
    // A recycled Member and logical-note slot must not change the old tail.
    actual.expression(0.0f);
    actual.on(81);
    CHECK(rms(actual.render()) == Approx(rms(reference.render())).margin(1e-6));
    actual.expression(1.0f);
    CHECK(rms(actual.render()) == Approx(rms(reference.render())).margin(1e-6));
    if (pedal) {
        actual.pedal(pedal, 0); reference.pedal(pedal, 0);
        CHECK(rms(actual.render()) == Approx(rms(reference.render())).margin(1e-6));
    }
}

TEST_CASE("[MPE release expression] Note Off preserves within-block expression history")
{
    const int mapping = GENERATE(0, 1, 2, 3);
    ExpressionSynth actual(mapping), reference(mapping);
    for (auto* f : { &actual, &reference }) {
        f->pedal(64);
        f->expression(0.75f);
        f->on(); f->render();
        f->expression(0.25f, 1, 64);
        f->expression(0.5f, 1, 128);
    }
    actual.off(192);
    actual.expression(1, 1, 200);
    actual.on(81, 200);
    auto a = actual.render(1), b = reference.render(1);
    for (int i = 0; i < 200; ++i) REQUIRE(a[i] == Approx(b[i]).margin(1e-6));
}

TEST_CASE("[MPE release expression] Live Manager raises held and released expression")
{
    const int mapping = GENERATE(0, 1, 2, 3);
    const bool released = GENERATE(false, true);
    ExpressionSynth f(mapping);
    f.pedal(64); f.expression(0.75f); f.on(); f.render();
    if (released) f.off();
    const double baseline = rms(f.render());
    f.expression(1.0f, 0);
    const double raised = rms(f.render());
    // A filter's resonance can make level non-monotonic as cutoff rises.
    if (mapping == 1) CHECK(std::abs(raised - baseline) > baseline * 0.05);
    else CHECK(raised > baseline * 1.2);
    f.expression(0.25f, 0);
    CHECK(rms(f.render()) == Approx(baseline).epsilon(0.003));
    // A fresh voice must not inherit the retired voice's expression snapshot.
    sfizz_all_sound_off(f.synth.get());
    f.expression(0.25f); f.on();
    CHECK(rms(f.render()) < baseline * 0.6);
}

TEST_CASE("[MPE release expression] Manager and Member ramps combine at their crossing")
{
    sfz::Synth synth;
    synth.setSamplesPerBlock(256);
    synth.setMPEEnabled(true);
    REQUIRE(synth.loadSfzString("mpe-ramp.sfz", "<region> sample=*sine cutoff_oncc74=1200"));
    synth.noteOn(0, 1, 69, 100);
    synth.hdChannelAftertouch(0, 1, 0.8f);
    synth.hdChannelAftertouch(0, 0, 0.2f);
    synth.hdChannelAftertouch(128, 1, 0.2f);
    synth.hdChannelAftertouch(128, 0, 0.8f);
    synth.hdcc(0, 1, 74, 0.8f); synth.hdcc(0, 0, 74, 0.2f);
    synth.hdcc(128, 1, 74, 0.2f); synth.hdcc(128, 0, 74, 0.8f);
    const auto* voice = synth.getActiveVoices().front();
    float values[256];
    for (int cc : { 74, 129 }) {
        sfz::linearEnvelope(voice->getControllerEvents(cc), absl::MakeSpan(values), [](float v) { return v; });
        CHECK(values[0] == Approx(0.8f));
        CHECK(values[64] == Approx(0.5f));
        CHECK(values[128] == Approx(0.8f));
    }
}
