add_executable(sfizz_midi_dispatch_benchmark main.cpp)
target_link_libraries(sfizz_midi_dispatch_benchmark
    PRIVATE sfizz::internal sfizz::filesystem absl::span absl::algorithm)
sfizz_enable_fast_math(sfizz_midi_dispatch_benchmark)
