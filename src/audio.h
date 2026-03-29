#pragma once

#include <cstdint>
#include <string>
#include <windows.h>

namespace mh2modern::audio {

struct SpeakerModeSelection {
    int effective_mode;
    bool changed;
    bool consumed_pending_force_stereo;
};

std::uint32_t resolve_sample_rate_hz(std::uint32_t requested_hz, std::uint32_t forced_hz);
std::uint32_t resolve_reported_fmod_version(std::uint32_t actual_version,
                                            std::uint32_t forced_version);
std::uint32_t normalize_dsp_buffer_length(std::uint32_t requested_length,
                                          std::uint32_t minimum_length);
std::uint32_t normalize_dsp_buffer_count(std::uint32_t requested_count,
                                         std::uint32_t minimum_count);
bool should_log_bounded_audio_sample(std::uint32_t seen_count, std::uint32_t sample_limit);
SpeakerModeSelection choose_speaker_mode_request(int requested_mode,
                                                 bool pending_force_stereo);
int describe_optional_fmod_bool(const bool* value);
std::string describe_fmod_channel_index(int channel_index);
std::string format_fmod_state_value(std::uint32_t value);
bool is_known_redundant_audio_setter_callsite(std::uint32_t caller_rva);
bool should_elide_redundant_audio_value(bool cache_valid, std::uint32_t cached_bits,
                                        std::uint32_t incoming_bits);
std::string format_fmod_version(std::uint32_t version);
bool should_force_stereo_fallback(int fmod_result, std::uint32_t caps, int speaker_mode,
                                  bool prefer_stereo_fallback);
bool should_inject_missing_dsp_buffer_fix(int fmod_result, std::uint32_t caps,
                                          bool fix_missing_dsp_buffer);
std::uint32_t relative_virtual_address(std::uintptr_t module_base, std::uint32_t module_size,
                                       std::uintptr_t address);

bool install_fmod_hooks(HMODULE game_module, bool enable_direct_audio_init_patch,
                        bool log_audio_init_calls,
                        bool log_audio_runtime_calls,
                        bool enable_audio_setter_elision,
                        bool log_audio_setter_elision,
                        bool prefer_stereo_fallback,
                        bool fix_missing_dsp_buffer,
                        std::uint32_t force_sample_rate_hz,
                        std::uint32_t force_reported_fmod_version,
                        std::uint32_t min_dsp_buffer_length,
                        std::uint32_t min_dsp_buffer_count,
                        std::uint32_t runtime_log_sample_limit);

}  // namespace mh2modern::audio
