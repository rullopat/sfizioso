// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#pragma once
#include "sfizz.h"
#include "sfizz.hpp"
#include "Synth.h"
#include <atomic>

struct sfizz_c_sample_reader final : sfz::SampleReader {
    sfizz_sample_reader_t* callback = nullptr;
    void* userData = nullptr;

    sfz::SampleData read(const std::string& path) override
    {
        if (callback == nullptr)
            return {};

        const void* data = nullptr;
        size_t size = 0;
        if (!callback(userData, path.c_str(), &data, &size))
            return {};

        return { data, size };
    }
};

struct sfizz_synth_t {
public:
    sfizz_synth_t() : rc{1} {}

    sfizz_synth_t(const sfizz_synth_t&) = delete;
    sfizz_synth_t& operator=(const sfizz_synth_t&) = delete;
    sfizz_synth_t(sfizz_synth_t&&) = delete;
    sfizz_synth_t& operator=(sfizz_synth_t&&) = delete;

private:
    ~sfizz_synth_t() {}

public:
    void remember()
    {
        rc.fetch_add(1, std::memory_order_relaxed);
    }

    void forget()
    {
        if (rc.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete this;
    }

    sfz::Synth synth;
    sfizz_c_sample_reader cSampleReader;
    std::atomic<size_t> rc;
};
