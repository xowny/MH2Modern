#include "audio.h"

#include "iat_hook.h"
#include "logger.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <sstream>
#include <type_traits>
#include <unordered_map>

namespace mh2modern::audio {
namespace {

constexpr const char* kFmodExModuleName = "fmodex.dll";
constexpr const char* kFmodEventModuleName = "fmod_event.dll";
constexpr const char* kSetSoftwareFormatProc =
    "?setSoftwareFormat@System@FMOD@@QAG?AW4FMOD_RESULT@@HW4FMOD_SOUND_FORMAT@@HHW4FMOD_DSP_RESAMPLER@@@Z";
constexpr const char* kSetSpeakerModeProc =
    "?setSpeakerMode@System@FMOD@@QAG?AW4FMOD_RESULT@@W4FMOD_SPEAKERMODE@@@Z";
constexpr const char* kSetDspBufferSizeProc =
    "?setDSPBufferSize@System@FMOD@@QAG?AW4FMOD_RESULT@@IH@Z";
constexpr const char* kGetDriverCapsProc =
    "?getDriverCaps@System@FMOD@@QAG?AW4FMOD_RESULT@@HPAIPAH1PAW4FMOD_SPEAKERMODE@@@Z";
constexpr const char* kSetSoftwareChannelsProc =
    "?setSoftwareChannels@System@FMOD@@QAG?AW4FMOD_RESULT@@H@Z";
constexpr const char* kGetVersionProc =
    "?getVersion@System@FMOD@@QAG?AW4FMOD_RESULT@@PAI@Z";
constexpr const char* kCreateStreamProc =
    "?createStream@System@FMOD@@QAG?AW4FMOD_RESULT@@PBDIPAUFMOD_CREATESOUNDEXINFO@@PAPAVSound@2@@Z";
constexpr const char* kCreateSoundProc =
    "?createSound@System@FMOD@@QAG?AW4FMOD_RESULT@@PBDIPAUFMOD_CREATESOUNDEXINFO@@PAPAVSound@2@@Z";
constexpr const char* kPlaySoundProc =
    "?playSound@System@FMOD@@QAG?AW4FMOD_RESULT@@W4FMOD_CHANNELINDEX@@PAVSound@2@_NPAPAVChannel@2@@Z";
constexpr const char* kGetOpenStateProc =
    // FMOD Ex 4.8.5 exports the 3-argument variant:
    // FMOD::Sound::getOpenState(FMOD_OPENSTATE*, unsigned int*, bool*)
    "?getOpenState@Sound@FMOD@@QAG?AW4FMOD_RESULT@@PAW4FMOD_OPENSTATE@@PAIPA_N@Z";
constexpr const char* kSet3DListenerAttributesProc =
    "?set3DListenerAttributes@System@FMOD@@QAG?AW4FMOD_RESULT@@HPBUFMOD_VECTOR@@000@Z";
constexpr const char* kEventStartProc =
    "?start@Event@FMOD@@QAG?AW4FMOD_RESULT@@XZ";
constexpr const char* kEventGetStateProc =
    "?getState@Event@FMOD@@QAG?AW4FMOD_RESULT@@PAI@Z";
constexpr const char* kEventSetPausedProc =
    "?setPaused@Event@FMOD@@QAG?AW4FMOD_RESULT@@_N@Z";
constexpr const char* kEventSetVolumeProc =
    "?setVolume@Event@FMOD@@QAG?AW4FMOD_RESULT@@M@Z";
constexpr const char* kEventSetPitchProc =
    "?setPitch@Event@FMOD@@QAG?AW4FMOD_RESULT@@M@Z";
constexpr const char* kEventStopProc =
    "?stop@Event@FMOD@@QAG?AW4FMOD_RESULT@@_N@Z";
constexpr const char* kEventSet3DAttributesProc =
    "?set3DAttributes@Event@FMOD@@QAG?AW4FMOD_RESULT@@PBUFMOD_VECTOR@@00@Z";
constexpr const char* kChannelStopProc =
    "?stop@Channel@FMOD@@QAG?AW4FMOD_RESULT@@XZ";
constexpr const char* kChannelSetPausedProc =
    "?setPaused@Channel@FMOD@@QAG?AW4FMOD_RESULT@@_N@Z";
constexpr const char* kChannelSetVolumeProc =
    "?setVolume@Channel@FMOD@@QAG?AW4FMOD_RESULT@@M@Z";
constexpr const char* kChannelSetFrequencyProc =
    "?setFrequency@Channel@FMOD@@QAG?AW4FMOD_RESULT@@M@Z";
constexpr const char* kChannelSet3DMinMaxDistanceProc =
    "?set3DMinMaxDistance@Channel@FMOD@@QAG?AW4FMOD_RESULT@@MM@Z";
constexpr const char* kChannelSet3DAttributesProc =
    "?set3DAttributes@Channel@FMOD@@QAG?AW4FMOD_RESULT@@PBUFMOD_VECTOR@@0@Z";
constexpr std::uintptr_t kAudioInitGetVersionCallRva = 0xCE544;
constexpr std::uintptr_t kAudioInitSetSoftwareChannelsCallRva = 0xCE55B;
constexpr std::uintptr_t kAudioInitGetDriverCapsCallRva = 0xCE57D;
constexpr std::uintptr_t kAudioInitSetDspBufferSizeCallRva = 0xCE5AB;
constexpr std::uintptr_t kAudioInitSetSpeakerModeCallRva = 0xCE5CA;
constexpr std::uintptr_t kAudioInitSetSoftwareFormatCallRva = 0xCE5F6;
constexpr std::uintptr_t kAudioInitSetSpeakerModeRetryCallRva = 0xCE62F;
constexpr std::uint32_t kRedundantEventSetVolumeCallRva = 0xCE322;
constexpr std::uint32_t kRedundantEventSetPitchCallRva = 0xCE3BA;
constexpr std::uint32_t kRedundantChannelSetVolumeCallRva = 0xC3F6B;
constexpr std::uint32_t kRedundantChannelSetPausedCallRva = 0xC4CFE;
constexpr std::uintptr_t kSetSoftwareChannelsThunkRva = 0x215956;
constexpr std::uintptr_t kGetDriverCapsThunkRva = 0x215950;
constexpr std::uintptr_t kSetDspBufferSizeThunkRva = 0x21594A;
constexpr std::uintptr_t kSetSpeakerModeThunkRva = 0x215944;
constexpr std::uintptr_t kSetSoftwareFormatThunkRva = 0x21593E;
constexpr std::uintptr_t kGetVersionThunkRva = 0x21595C;
constexpr int kFmodOk = 0;

enum class SpeakerMode : int {
    Default = 0,
    Raw = 1,
    Mono = 2,
    Stereo = 3,
    Quad = 4,
    Surround = 5,
    FivePointOne = 6,
    SevenPointOne = 7,
    SevenPointOnePointFour = 8,
};

enum class SoundFormat : int {
    None = 0,
    Pcm8 = 1,
    Pcm16 = 2,
    Pcm24 = 3,
    Pcm32 = 4,
    PcmFloat = 5,
    Bitstream = 6,
};

using FmodResult = int;
using SetSoftwareFormatFn =
    FmodResult(__stdcall*)(void*, int, int, int, int, int);
using SetSpeakerModeFn = FmodResult(__stdcall*)(void*, int);
using SetDspBufferSizeFn = FmodResult(__stdcall*)(void*, unsigned int, int);
using GetDriverCapsFn =
    FmodResult(__stdcall*)(void*, int, unsigned int*, int*, int*, int*);
using SetSoftwareChannelsFn = FmodResult(__stdcall*)(void*, int);
using GetVersionFn = FmodResult(__stdcall*)(void*, unsigned int*);
using CreateStreamFn = FmodResult(__stdcall*)(void*, const char*, unsigned int, void*, void**);
using CreateSoundFn = FmodResult(__stdcall*)(void*, const char*, unsigned int, void*, void**);
using PlaySoundFn = FmodResult(__stdcall*)(void*, int, void*, int, void**);
using GetOpenStateFn = FmodResult(__stdcall*)(void*, int*, unsigned int*, bool*);
using Set3DListenerAttributesFn =
    FmodResult(__stdcall*)(void*, int, const void*, const void*, const void*, const void*);
using EventStartFn = FmodResult(__stdcall*)(void*);
using EventGetStateFn = FmodResult(__stdcall*)(void*, unsigned int*);
using EventSetPausedFn = FmodResult(__stdcall*)(void*, bool);
using EventSetVolumeFn = FmodResult(__stdcall*)(void*, float);
using EventSetPitchFn = FmodResult(__stdcall*)(void*, float);
using EventStopFn = FmodResult(__stdcall*)(void*, int);
using EventSet3DAttributesFn =
    FmodResult(__stdcall*)(void*, const void*, const void*, const void*);
using ChannelStopFn = FmodResult(__stdcall*)(void*);
using ChannelSetPausedFn = FmodResult(__stdcall*)(void*, bool);
using ChannelSetVolumeFn = FmodResult(__stdcall*)(void*, float);
using ChannelSetFrequencyFn = FmodResult(__stdcall*)(void*, float);
using ChannelSet3DMinMaxDistanceFn = FmodResult(__stdcall*)(void*, float, float);
using ChannelSet3DAttributesFn = FmodResult(__stdcall*)(void*, const void*, const void*);

SetSoftwareFormatFn g_original_set_software_format = nullptr;
SetSpeakerModeFn g_original_set_speaker_mode = nullptr;
SetDspBufferSizeFn g_original_set_dsp_buffer_size = nullptr;
GetDriverCapsFn g_original_get_driver_caps = nullptr;
SetSoftwareChannelsFn g_original_set_software_channels = nullptr;
GetVersionFn g_original_get_version = nullptr;
CreateStreamFn g_original_create_stream = nullptr;
CreateSoundFn g_original_create_sound = nullptr;
PlaySoundFn g_original_play_sound = nullptr;
GetOpenStateFn g_original_get_open_state = nullptr;
Set3DListenerAttributesFn g_original_set_3d_listener_attributes = nullptr;
EventStartFn g_original_event_start = nullptr;
EventGetStateFn g_original_event_get_state = nullptr;
EventSetPausedFn g_original_event_set_paused = nullptr;
EventSetVolumeFn g_original_event_set_volume = nullptr;
EventSetPitchFn g_original_event_set_pitch = nullptr;
EventStopFn g_original_event_stop = nullptr;
EventSet3DAttributesFn g_original_event_set_3d_attributes = nullptr;
ChannelStopFn g_original_channel_stop = nullptr;
ChannelSetPausedFn g_original_channel_set_paused = nullptr;
ChannelSetVolumeFn g_original_channel_set_volume = nullptr;
ChannelSetFrequencyFn g_original_channel_set_frequency = nullptr;
ChannelSet3DMinMaxDistanceFn g_original_channel_set_3d_min_max_distance = nullptr;
ChannelSet3DAttributesFn g_original_channel_set_3d_attributes = nullptr;

static_assert(std::is_same_v<GetOpenStateFn, FmodResult(__stdcall*)(void*, int*, unsigned int*, bool*)>,
              "FMOD 4.8.5 getOpenState export takes 3 parameters after this");

bool g_log_audio_init_calls = true;
bool g_log_audio_runtime_calls = true;
bool g_enable_audio_setter_elision = true;
bool g_log_audio_setter_elision = true;
bool g_prefer_stereo_fallback = true;
bool g_fix_missing_dsp_buffer = true;
bool g_last_driver_caps_forced_stereo = false;
bool g_missing_dsp_buffer_fix_pending = false;
std::uint32_t g_force_sample_rate_hz = 0;
std::uint32_t g_force_reported_fmod_version = 0;
std::uint32_t g_min_dsp_buffer_length = 0;
std::uint32_t g_min_dsp_buffer_count = 0;
std::uint32_t g_runtime_log_sample_limit = 16;
HMODULE g_game_module = nullptr;
std::uint32_t g_game_module_size = 0;

std::atomic<std::uint32_t> g_create_stream_log_count{0};
std::atomic<std::uint32_t> g_create_sound_log_count{0};
std::atomic<std::uint32_t> g_play_sound_log_count{0};
std::atomic<std::uint32_t> g_get_open_state_log_count{0};
std::atomic<std::uint32_t> g_set_3d_listener_log_count{0};
std::atomic<std::uint32_t> g_event_start_log_count{0};
std::atomic<std::uint32_t> g_event_get_state_log_count{0};
std::atomic<std::uint32_t> g_event_set_paused_log_count{0};
std::atomic<std::uint32_t> g_event_set_volume_log_count{0};
std::atomic<std::uint32_t> g_event_set_pitch_log_count{0};
std::atomic<std::uint32_t> g_event_stop_log_count{0};
std::atomic<std::uint32_t> g_event_set_3d_log_count{0};
std::atomic<std::uint32_t> g_channel_stop_log_count{0};
std::atomic<std::uint32_t> g_channel_set_paused_log_count{0};
std::atomic<std::uint32_t> g_channel_set_volume_log_count{0};
std::atomic<std::uint32_t> g_channel_set_frequency_log_count{0};
std::atomic<std::uint32_t> g_channel_set_3d_min_max_log_count{0};
std::atomic<std::uint32_t> g_channel_set_3d_log_count{0};
std::atomic<std::uint32_t> g_audio_setter_elision_log_count{0};

using SetterCacheMap = std::unordered_map<void*, std::uint32_t>;

std::mutex g_audio_setter_cache_mutex;
SetterCacheMap g_event_volume_cache;
SetterCacheMap g_event_pitch_cache;
SetterCacheMap g_channel_paused_cache;
SetterCacheMap g_channel_volume_cache;

std::uint32_t float_bits(float value) {
    static_assert(sizeof(value) == sizeof(std::uint32_t));
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

std::string format_float_argument(float value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

std::string format_game_callsite_rva(std::uint32_t caller_rva) {
    if (caller_rva == 0) {
        return "unknown";
    }

    std::ostringstream oss;
    oss << "Manhunt2.exe+0x" << std::hex << caller_rva << std::dec;
    return oss.str();
}

std::uint32_t capture_game_caller_rva() {
    if (g_game_module == nullptr || g_game_module_size == 0) {
        return 0;
    }

    void* frames[1]{};
    const auto captured = CaptureStackBackTrace(2, static_cast<DWORD>(std::size(frames)), frames, nullptr);
    if (captured == 0 || frames[0] == nullptr) {
        return 0;
    }

    return relative_virtual_address(
        reinterpret_cast<std::uintptr_t>(g_game_module), g_game_module_size,
        reinterpret_cast<std::uintptr_t>(frames[0]));
}

bool should_log_audio_setter_elision_sample() {
    if (!g_log_audio_setter_elision) {
        return false;
    }

    const auto seen_count = g_audio_setter_elision_log_count.fetch_add(1, std::memory_order_relaxed);
    return g_runtime_log_sample_limit == 0 || seen_count < g_runtime_log_sample_limit;
}

void log_audio_setter_elision(const char* setter_name, const char* handle_name, void* handle,
                              const char* value_name, const std::string& value_repr,
                              std::uint32_t caller_rva) {
    if (!should_log_audio_setter_elision_sample()) {
        return;
    }

    std::ostringstream oss;
    oss << "Elided redundant " << setter_name << "(" << handle_name << '=' << handle << ", "
        << value_name << '=' << value_repr << ") at " << format_game_callsite_rva(caller_rva);
    logger::info(oss.str());
}

bool should_elide_cached_value(const SetterCacheMap& cache, void* handle,
                               std::uint32_t incoming_bits) {
    if (handle == nullptr) {
        return false;
    }

    const auto it = cache.find(handle);
    const auto cached_bits = it != cache.end() ? it->second : 0U;
    return should_elide_redundant_audio_value(it != cache.end(), cached_bits, incoming_bits);
}

void remember_cached_value(SetterCacheMap& cache, void* handle, std::uint32_t bits) {
    if (handle == nullptr) {
        return;
    }

    cache[handle] = bits;
}

void clear_cached_value(SetterCacheMap& cache, void* handle) {
    if (handle == nullptr) {
        return;
    }

    cache.erase(handle);
}

void clear_event_audio_setter_cache(void* event_handle) {
    if (event_handle == nullptr) {
        return;
    }

    std::lock_guard lock(g_audio_setter_cache_mutex);
    clear_cached_value(g_event_volume_cache, event_handle);
    clear_cached_value(g_event_pitch_cache, event_handle);
}

void clear_channel_audio_setter_cache(void* channel) {
    if (channel == nullptr) {
        return;
    }

    std::lock_guard lock(g_audio_setter_cache_mutex);
    clear_cached_value(g_channel_paused_cache, channel);
    clear_cached_value(g_channel_volume_cache, channel);
}

void clear_all_audio_setter_caches() {
    std::lock_guard lock(g_audio_setter_cache_mutex);
    g_event_volume_cache.clear();
    g_event_pitch_cache.clear();
    g_channel_paused_cache.clear();
    g_channel_volume_cache.clear();
}

bool should_elide_event_set_volume(void* event_handle, std::uint32_t caller_rva, float volume) {
    if (!g_enable_audio_setter_elision || caller_rva != kRedundantEventSetVolumeCallRva) {
        return false;
    }

    std::lock_guard lock(g_audio_setter_cache_mutex);
    return should_elide_cached_value(g_event_volume_cache, event_handle, float_bits(volume));
}

bool should_elide_event_set_pitch(void* event_handle, std::uint32_t caller_rva, float pitch) {
    if (!g_enable_audio_setter_elision || caller_rva != kRedundantEventSetPitchCallRva) {
        return false;
    }

    std::lock_guard lock(g_audio_setter_cache_mutex);
    return should_elide_cached_value(g_event_pitch_cache, event_handle, float_bits(pitch));
}

bool should_elide_channel_set_paused(void* channel, std::uint32_t caller_rva, bool paused) {
    if (!g_enable_audio_setter_elision || caller_rva != kRedundantChannelSetPausedCallRva) {
        return false;
    }

    std::lock_guard lock(g_audio_setter_cache_mutex);
    return should_elide_cached_value(g_channel_paused_cache, channel, paused ? 1U : 0U);
}

bool should_elide_channel_set_volume(void* channel, std::uint32_t caller_rva, float volume) {
    if (!g_enable_audio_setter_elision || caller_rva != kRedundantChannelSetVolumeCallRva) {
        return false;
    }

    std::lock_guard lock(g_audio_setter_cache_mutex);
    return should_elide_cached_value(g_channel_volume_cache, channel, float_bits(volume));
}

void remember_event_set_volume(void* event_handle, float volume) {
    std::lock_guard lock(g_audio_setter_cache_mutex);
    remember_cached_value(g_event_volume_cache, event_handle, float_bits(volume));
}

void remember_event_set_pitch(void* event_handle, float pitch) {
    std::lock_guard lock(g_audio_setter_cache_mutex);
    remember_cached_value(g_event_pitch_cache, event_handle, float_bits(pitch));
}

void remember_channel_set_paused(void* channel, bool paused) {
    std::lock_guard lock(g_audio_setter_cache_mutex);
    remember_cached_value(g_channel_paused_cache, channel, paused ? 1U : 0U);
}

void remember_channel_set_volume(void* channel, float volume) {
    std::lock_guard lock(g_audio_setter_cache_mutex);
    remember_cached_value(g_channel_volume_cache, channel, float_bits(volume));
}

std::uint32_t module_size(HMODULE module) {
    if (module == nullptr) {
        return 0;
    }

    auto* base = reinterpret_cast<std::uint8_t*>(module);
    const auto* dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        return 0;
    }

    const auto* nt_headers =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        return 0;
    }

    return nt_headers->OptionalHeader.SizeOfImage;
}

std::string filename_only(const char* path) {
    if (path == nullptr || *path == '\0') {
        return {};
    }

    std::string value(path);
    const auto slash = value.find_last_of("\\/");
    return slash == std::string::npos ? value : value.substr(slash + 1);
}

std::string describe_address(std::uintptr_t address) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(address), &module)) {
        std::ostringstream oss;
        oss << "0x" << std::hex << address << std::dec;
        return oss.str();
    }

    char module_path[MAX_PATH]{};
    GetModuleFileNameA(module, module_path, MAX_PATH);

    std::ostringstream oss;
    oss << filename_only(module_path);
    const auto size = module_size(module);
    const auto rva = relative_virtual_address(
        reinterpret_cast<std::uintptr_t>(module), size, address);
    if (rva != 0) {
        oss << "+0x" << std::hex << rva << std::dec;
    }
    return oss.str();
}

std::string caller_suffix() {
    void* frames[4]{};
    const auto captured = CaptureStackBackTrace(1, static_cast<DWORD>(std::size(frames)), frames, nullptr);
    if (captured == 0) {
        return {};
    }

    std::ostringstream oss;
    oss << ", stack=";
    for (USHORT i = 0; i < captured; ++i) {
        if (i != 0) {
            oss << " <- ";
        }
        oss << describe_address(reinterpret_cast<std::uintptr_t>(frames[i]));
    }
    return oss.str();
}

const char* speaker_mode_name(int mode) {
    switch (static_cast<SpeakerMode>(mode)) {
    case SpeakerMode::Default:
        return "default";
    case SpeakerMode::Raw:
        return "raw";
    case SpeakerMode::Mono:
        return "mono";
    case SpeakerMode::Stereo:
        return "stereo";
    case SpeakerMode::Quad:
        return "quad";
    case SpeakerMode::Surround:
        return "surround";
    case SpeakerMode::FivePointOne:
        return "5.1";
    case SpeakerMode::SevenPointOne:
        return "7.1";
    case SpeakerMode::SevenPointOnePointFour:
        return "7.1.4";
    default:
        return "unknown";
    }
}

const char* sound_format_name(int format) {
    switch (static_cast<SoundFormat>(format)) {
    case SoundFormat::None:
        return "none";
    case SoundFormat::Pcm8:
        return "pcm8";
    case SoundFormat::Pcm16:
        return "pcm16";
    case SoundFormat::Pcm24:
        return "pcm24";
    case SoundFormat::Pcm32:
        return "pcm32";
    case SoundFormat::PcmFloat:
        return "pcmfloat";
    case SoundFormat::Bitstream:
        return "bitstream";
    default:
        return "unknown";
    }
}

void log_hook_result(const char* module_name, const char* proc_name, bool ok) {
    std::ostringstream oss;
    oss << (ok ? "Patched " : "Failed to patch ") << module_name << '!' << proc_name;
    if (ok) {
        logger::info(oss.str());
    } else {
        logger::error(oss.str());
    }
}

bool resolve_relative_call_target(std::uintptr_t address, void** target_out) {
    auto* code = reinterpret_cast<std::uint8_t*>(address);
    if (code[0] != 0xE8) {
        return false;
    }

    const auto rel = *reinterpret_cast<std::int32_t*>(code + 1);
    const auto target = reinterpret_cast<void*>(
        address + 5 + static_cast<std::intptr_t>(rel));
    if (target_out != nullptr) {
        *target_out = target;
    }
    return true;
}

bool patch_relative_call(std::uintptr_t address, void* replacement, void** original_target_out) {
    void* original_target = nullptr;
    if (!resolve_relative_call_target(address, &original_target)) {
        return false;
    }

    const auto replacement_rel =
        reinterpret_cast<std::intptr_t>(replacement) - static_cast<std::intptr_t>(address + 5);
    if (replacement_rel < INT32_MIN || replacement_rel > INT32_MAX) {
        return false;
    }

    DWORD old_protect{};
    auto* code = reinterpret_cast<std::uint8_t*>(address);
    if (!VirtualProtect(code, 5, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }

    code[0] = 0xE8;
    *reinterpret_cast<std::int32_t*>(code + 1) = static_cast<std::int32_t>(replacement_rel);

    DWORD restored_protect{};
    VirtualProtect(code, 5, old_protect, &restored_protect);
    FlushInstructionCache(GetCurrentProcess(), code, 5);

    if (original_target_out != nullptr) {
        *original_target_out = original_target;
    }
    return true;
}

void log_direct_patch_result(const char* label, std::uintptr_t rva, bool ok) {
    std::ostringstream oss;
    oss << (ok ? "Patched direct audio init call " : "Failed to patch direct audio init call ")
        << label << " at Manhunt2.exe+0x" << std::hex << rva << std::dec;
    if (ok) {
        logger::info(oss.str());
    } else {
        logger::error(oss.str());
    }
}

bool preflight_direct_patch_callsite(HMODULE game_module, std::uintptr_t call_rva,
                                     std::uintptr_t expected_target_rva, const char* label) {
    void* target = nullptr;
    const auto address = reinterpret_cast<std::uintptr_t>(game_module) + call_rva;
    if (!resolve_relative_call_target(address, &target)) {
        std::ostringstream oss;
        oss << "Direct audio init preflight failed for " << label
            << ": call opcode mismatch at Manhunt2.exe+0x" << std::hex << call_rva << std::dec;
        logger::error(oss.str());
        return false;
    }

    const auto expected_target =
        reinterpret_cast<std::uintptr_t>(game_module) + expected_target_rva;
    if (reinterpret_cast<std::uintptr_t>(target) != expected_target) {
        std::ostringstream oss;
        oss << "Direct audio init preflight failed for " << label
            << ": expected target Manhunt2.exe+0x" << std::hex << expected_target_rva
            << " but saw 0x" << reinterpret_cast<std::uintptr_t>(target) << std::dec;
        logger::error(oss.str());
        return false;
    }

    return true;
}

}  // namespace

std::uint32_t resolve_sample_rate_hz(std::uint32_t requested_hz, std::uint32_t forced_hz) {
    return forced_hz == 0 ? requested_hz : forced_hz;
}

std::uint32_t resolve_reported_fmod_version(std::uint32_t actual_version,
                                            std::uint32_t forced_version) {
    return forced_version == 0 ? actual_version : forced_version;
}

std::uint32_t normalize_dsp_buffer_length(std::uint32_t requested_length,
                                          std::uint32_t minimum_length) {
    return minimum_length == 0 ? requested_length : (std::max)(requested_length, minimum_length);
}

std::uint32_t normalize_dsp_buffer_count(std::uint32_t requested_count,
                                         std::uint32_t minimum_count) {
    return minimum_count == 0 ? requested_count : (std::max)(requested_count, minimum_count);
}

bool should_log_bounded_audio_sample(std::uint32_t seen_count, std::uint32_t sample_limit) {
    return sample_limit == 0 || seen_count < sample_limit;
}

SpeakerModeSelection choose_speaker_mode_request(int requested_mode,
                                                 bool pending_force_stereo) {
    const auto effective_mode =
        pending_force_stereo && requested_mode == static_cast<int>(SpeakerMode::Mono)
            ? static_cast<int>(SpeakerMode::Stereo)
            : requested_mode;

    return SpeakerModeSelection{
        effective_mode,
        effective_mode != requested_mode,
        pending_force_stereo,
    };
}

int describe_optional_fmod_bool(const bool* value) {
    if (value == nullptr) {
        return -1;
    }
    return *value ? 1 : 0;
}

std::string describe_fmod_channel_index(int channel_index) {
    if (channel_index == -1) {
        return "free";
    }

    return std::to_string(channel_index);
}

std::string format_fmod_state_value(std::uint32_t value) {
    std::ostringstream oss;
    oss << "0x" << std::hex << value << std::dec;
    return oss.str();
}

bool is_known_redundant_audio_setter_callsite(std::uint32_t caller_rva) {
    switch (caller_rva) {
    case kRedundantEventSetVolumeCallRva:
    case kRedundantEventSetPitchCallRva:
    case kRedundantChannelSetVolumeCallRva:
    case kRedundantChannelSetPausedCallRva:
        return true;
    default:
        return false;
    }
}

bool should_elide_redundant_audio_value(bool cache_valid, std::uint32_t cached_bits,
                                        std::uint32_t incoming_bits) {
    return cache_valid && cached_bits == incoming_bits;
}

std::string format_fmod_version(std::uint32_t version) {
    std::ostringstream oss;
    oss << ((version >> 16U) & 0xffU) << '.' << ((version >> 8U) & 0xffU) << '.'
        << (version & 0xffU);
    return oss.str();
}

bool should_log_runtime_sample(std::atomic<std::uint32_t>& counter) {
    if (!g_log_audio_runtime_calls) {
        return false;
    }
    const auto seen_count = counter.fetch_add(1, std::memory_order_relaxed);
    return should_log_bounded_audio_sample(seen_count, g_runtime_log_sample_limit);
}

bool should_force_stereo_fallback(int fmod_result, std::uint32_t caps, int speaker_mode,
                                  bool prefer_stereo_fallback) {
    return prefer_stereo_fallback && fmod_result == 0 && caps == 0 &&
           speaker_mode == static_cast<int>(SpeakerMode::Mono);
}

bool should_inject_missing_dsp_buffer_fix(int fmod_result, std::uint32_t caps,
                                          bool fix_missing_dsp_buffer) {
    return fix_missing_dsp_buffer && fmod_result == 0 && (caps & 0x2U) == 0;
}

std::uint32_t relative_virtual_address(std::uintptr_t module_base, std::uint32_t module_size,
                                       std::uintptr_t address) {
    if (module_base == 0 || module_size == 0 || address < module_base) {
        return 0;
    }
    const auto delta = address - module_base;
    if (delta >= module_size) {
        return 0;
    }
    return static_cast<std::uint32_t>(delta);
}

extern "C" FmodResult __stdcall hooked_get_driver_caps(void* system, int id, unsigned int* caps,
                                                       int* min_frequency, int* max_frequency,
                                                       int* speaker_mode) {
    const auto result =
        g_original_get_driver_caps(system, id, caps, min_frequency, max_frequency, speaker_mode);

    const auto caps_value = caps != nullptr ? *caps : 0U;
    const auto speaker_mode_value = speaker_mode != nullptr ? *speaker_mode : -1;
    g_missing_dsp_buffer_fix_pending =
        should_inject_missing_dsp_buffer_fix(result, caps_value, g_fix_missing_dsp_buffer);
    g_last_driver_caps_forced_stereo =
        speaker_mode != nullptr &&
        should_force_stereo_fallback(result, caps_value, speaker_mode_value, g_prefer_stereo_fallback);
    if (g_last_driver_caps_forced_stereo) {
        *speaker_mode = static_cast<int>(SpeakerMode::Stereo);
    }

    if (g_log_audio_init_calls) {
        std::ostringstream oss;
        oss << "FMOD getDriverCaps(id=" << id << ") -> result=" << result
            << ", caps=0x" << std::hex << caps_value << std::dec
            << ", min_hz=" << (min_frequency != nullptr ? *min_frequency : 0)
            << ", max_hz=" << (max_frequency != nullptr ? *max_frequency : 0)
            << ", speaker_mode=" << speaker_mode_name(speaker_mode_value) << '('
            << speaker_mode_value << ')';
        if (g_last_driver_caps_forced_stereo) {
            oss << " -> stereo fallback";
        }
        if (g_missing_dsp_buffer_fix_pending) {
            oss << " -> missing-dsp-buffer fix pending";
        }
        oss << caller_suffix();
        logger::info(oss.str());
    }

    return result;
}

extern "C" FmodResult __stdcall hooked_set_speaker_mode(void* system, int speaker_mode) {
    if (g_missing_dsp_buffer_fix_pending && g_original_set_dsp_buffer_size != nullptr) {
        const auto dsp_result = g_original_set_dsp_buffer_size(
            system, g_min_dsp_buffer_length,
            static_cast<int>((std::max)(1U, g_min_dsp_buffer_count)));

        std::ostringstream dsp_oss;
        dsp_oss << "Injected FMOD setDSPBufferSize(length=" << g_min_dsp_buffer_length
                << ", count=" << (std::max)(1U, g_min_dsp_buffer_count)
                << ") before speaker mode -> result=" << dsp_result << caller_suffix();
        logger::info(dsp_oss.str());
    }
    g_missing_dsp_buffer_fix_pending = false;

    const auto selection =
        choose_speaker_mode_request(speaker_mode, g_last_driver_caps_forced_stereo);
    if (selection.consumed_pending_force_stereo) {
        g_last_driver_caps_forced_stereo = false;
    }

    const auto result = g_original_set_speaker_mode(system, selection.effective_mode);

    if (g_log_audio_init_calls || selection.changed) {
        std::ostringstream oss;
        oss << "FMOD setSpeakerMode(mode=" << speaker_mode_name(speaker_mode) << '('
            << speaker_mode << ')';
        if (selection.changed) {
            oss << "->" << speaker_mode_name(selection.effective_mode) << '('
                << selection.effective_mode << ')';
        }
        oss << ") -> result=" << result << caller_suffix();
        logger::info(oss.str());
    }

    return result;
}

extern "C" FmodResult __stdcall hooked_get_version(void* system, unsigned int* version) {
    const auto result = g_original_get_version(system, version);
    const auto actual_value = version != nullptr ? *version : 0U;
    const auto effective_value =
        resolve_reported_fmod_version(actual_value, g_force_reported_fmod_version);
    const bool changed = version != nullptr && effective_value != actual_value;
    if (changed) {
        *version = effective_value;
    }

    if (g_log_audio_init_calls) {
        std::ostringstream oss;
        oss << "FMOD getVersion() -> result=" << result << ", version=0x" << std::hex
            << actual_value << std::dec << " (" << format_fmod_version(actual_value) << ')';
        if (changed) {
            oss << " -> 0x" << std::hex << effective_value << std::dec << " ("
                << format_fmod_version(effective_value) << ')';
        }
        oss << caller_suffix();
        logger::info(oss.str());
    }

    return result;
}

extern "C" FmodResult __stdcall hooked_create_stream(void* system, const char* name_or_data,
                                                     unsigned int mode, void* exinfo,
                                                     void** sound) {
    const auto result = g_original_create_stream(system, name_or_data, mode, exinfo, sound);
    if (should_log_runtime_sample(g_create_stream_log_count)) {
        std::ostringstream oss;
        oss << "FMOD createStream(name=" << (name_or_data != nullptr ? name_or_data : "<null>")
            << ", mode=0x" << std::hex << mode << std::dec << ", exinfo=" << exinfo
            << ") -> result=" << result << ", sound=" << (sound != nullptr ? *sound : nullptr)
            << caller_suffix();
        logger::info(oss.str());
    }
    return result;
}

extern "C" FmodResult __stdcall hooked_create_sound(void* system, const char* name_or_data,
                                                    unsigned int mode, void* exinfo,
                                                    void** sound) {
    const auto result = g_original_create_sound(system, name_or_data, mode, exinfo, sound);
    if (should_log_runtime_sample(g_create_sound_log_count)) {
        std::ostringstream oss;
        oss << "FMOD createSound(name=" << (name_or_data != nullptr ? name_or_data : "<null>")
            << ", mode=0x" << std::hex << mode << std::dec << ", exinfo=" << exinfo
            << ") -> result=" << result << ", sound=" << (sound != nullptr ? *sound : nullptr)
            << caller_suffix();
        logger::info(oss.str());
    }
    return result;
}

extern "C" FmodResult __stdcall hooked_play_sound(void* system, int channel_index, void* sound,
                                                  int paused, void** channel) {
    const auto result = g_original_play_sound(system, channel_index, sound, paused, channel);
    if (result == kFmodOk && channel != nullptr && *channel != nullptr) {
        clear_channel_audio_setter_cache(*channel);
    }
    if (should_log_runtime_sample(g_play_sound_log_count)) {
        std::ostringstream oss;
        oss << "FMOD playSound(channel_index=" << describe_fmod_channel_index(channel_index)
            << ", sound=" << sound
            << ", paused=" << paused << ") -> result=" << result
            << ", channel=" << (channel != nullptr ? *channel : nullptr) << caller_suffix();
        logger::info(oss.str());
    }
    return result;
}

extern "C" FmodResult __stdcall hooked_get_open_state(void* sound, int* open_state,
                                                      unsigned int* percent_buffered,
                                                      bool* starving) {
    const auto result = g_original_get_open_state(
        sound, open_state, percent_buffered, starving);
    if (should_log_runtime_sample(g_get_open_state_log_count)) {
        std::ostringstream oss;
        oss << "FMOD getOpenState(sound=" << sound << ") -> result=" << result
            << ", state=" << (open_state != nullptr ? *open_state : -1)
            << ", buffered=" << (percent_buffered != nullptr ? *percent_buffered : 0U)
            << ", starving=" << describe_optional_fmod_bool(starving) << caller_suffix();
        logger::info(oss.str());
    }
    return result;
}

extern "C" FmodResult __stdcall hooked_set_3d_listener_attributes(
    void* system, int listener, const void* position, const void* velocity, const void* forward,
    const void* up) {
    const auto result = g_original_set_3d_listener_attributes(
        system, listener, position, velocity, forward, up);
    if (should_log_runtime_sample(g_set_3d_listener_log_count)) {
        std::ostringstream oss;
        oss << "FMOD set3DListenerAttributes(listener=" << listener
            << ", pos=" << position << ", vel=" << velocity << ", fwd=" << forward
            << ", up=" << up << ") -> result=" << result << caller_suffix();
        logger::info(oss.str());
    }
    return result;
}

extern "C" FmodResult __stdcall hooked_event_start(void* event_handle) {
    clear_event_audio_setter_cache(event_handle);
    const auto result = g_original_event_start(event_handle);
    if (should_log_runtime_sample(g_event_start_log_count)) {
        std::ostringstream oss;
        oss << "FMOD::Event start(event=" << event_handle << ") -> result=" << result
            << caller_suffix();
        logger::info(oss.str());
    }
    return result;
}

extern "C" FmodResult __stdcall hooked_event_get_state(void* event_handle, unsigned int* state) {
    const auto result = g_original_event_get_state(event_handle, state);
    if (should_log_runtime_sample(g_event_get_state_log_count)) {
        std::ostringstream oss;
        oss << "FMOD::Event getState(event=" << event_handle << ") -> result=" << result
            << ", state=" << format_fmod_state_value(state != nullptr ? *state : 0U)
            << caller_suffix();
        logger::info(oss.str());
    }
    return result;
}

extern "C" FmodResult __stdcall hooked_event_set_paused(void* event_handle, bool paused) {
    const auto result = g_original_event_set_paused(event_handle, paused);
    if (should_log_runtime_sample(g_event_set_paused_log_count)) {
        std::ostringstream oss;
        oss << "FMOD::Event setPaused(event=" << event_handle << ", paused=" << (paused ? 1 : 0)
            << ") -> result=" << result << caller_suffix();
        logger::info(oss.str());
    }
    return result;
}

extern "C" FmodResult __stdcall hooked_event_set_volume(void* event_handle, float volume) {
    const auto caller_rva = capture_game_caller_rva();
    if (should_elide_event_set_volume(event_handle, caller_rva, volume)) {
        log_audio_setter_elision(
            "FMOD::Event setVolume", "event", event_handle, "volume",
            format_float_argument(volume), caller_rva);
        return kFmodOk;
    }

    const auto result = g_original_event_set_volume(event_handle, volume);
    if (result == kFmodOk) {
        remember_event_set_volume(event_handle, volume);
    }
    if (should_log_runtime_sample(g_event_set_volume_log_count)) {
        std::ostringstream oss;
        oss << "FMOD::Event setVolume(event=" << event_handle << ", volume=" << volume
            << ") -> result=" << result << caller_suffix();
        logger::info(oss.str());
    }
    return result;
}

extern "C" FmodResult __stdcall hooked_event_set_pitch(void* event_handle, float pitch) {
    const auto caller_rva = capture_game_caller_rva();
    if (should_elide_event_set_pitch(event_handle, caller_rva, pitch)) {
        log_audio_setter_elision(
            "FMOD::Event setPitch", "event", event_handle, "pitch",
            format_float_argument(pitch), caller_rva);
        return kFmodOk;
    }

    const auto result = g_original_event_set_pitch(event_handle, pitch);
    if (result == kFmodOk) {
        remember_event_set_pitch(event_handle, pitch);
    }
    if (should_log_runtime_sample(g_event_set_pitch_log_count)) {
        std::ostringstream oss;
        oss << "FMOD::Event setPitch(event=" << event_handle << ", pitch=" << pitch
            << ") -> result=" << result << caller_suffix();
        logger::info(oss.str());
    }
    return result;
}

extern "C" FmodResult __stdcall hooked_event_stop(void* event_handle, int immediate) {
    const auto result = g_original_event_stop(event_handle, immediate);
    if (result == kFmodOk) {
        clear_event_audio_setter_cache(event_handle);
    }
    if (should_log_runtime_sample(g_event_stop_log_count)) {
        std::ostringstream oss;
        oss << "FMOD::Event stop(event=" << event_handle << ", immediate=" << immediate
            << ") -> result=" << result << caller_suffix();
        logger::info(oss.str());
    }
    return result;
}

extern "C" FmodResult __stdcall hooked_channel_stop(void* channel) {
    const auto result = g_original_channel_stop(channel);
    if (result == kFmodOk) {
        clear_channel_audio_setter_cache(channel);
    }
    if (should_log_runtime_sample(g_channel_stop_log_count)) {
        std::ostringstream oss;
        oss << "FMOD::Channel stop(channel=" << channel << ") -> result=" << result
            << caller_suffix();
        logger::info(oss.str());
    }
    return result;
}

extern "C" FmodResult __stdcall hooked_channel_set_paused(void* channel, bool paused) {
    const auto caller_rva = capture_game_caller_rva();
    if (should_elide_channel_set_paused(channel, caller_rva, paused)) {
        log_audio_setter_elision(
            "FMOD::Channel setPaused", "channel", channel, "paused", paused ? "1" : "0",
            caller_rva);
        return kFmodOk;
    }

    const auto result = g_original_channel_set_paused(channel, paused);
    if (result == kFmodOk) {
        remember_channel_set_paused(channel, paused);
    }
    if (should_log_runtime_sample(g_channel_set_paused_log_count)) {
        std::ostringstream oss;
        oss << "FMOD::Channel setPaused(channel=" << channel << ", paused=" << (paused ? 1 : 0)
            << ") -> result=" << result << caller_suffix();
        logger::info(oss.str());
    }
    return result;
}

extern "C" FmodResult __stdcall hooked_channel_set_volume(void* channel, float volume) {
    const auto caller_rva = capture_game_caller_rva();
    if (should_elide_channel_set_volume(channel, caller_rva, volume)) {
        log_audio_setter_elision(
            "FMOD::Channel setVolume", "channel", channel, "volume",
            format_float_argument(volume), caller_rva);
        return kFmodOk;
    }

    const auto result = g_original_channel_set_volume(channel, volume);
    if (result == kFmodOk) {
        remember_channel_set_volume(channel, volume);
    }
    if (should_log_runtime_sample(g_channel_set_volume_log_count)) {
        std::ostringstream oss;
        oss << "FMOD::Channel setVolume(channel=" << channel << ", volume=" << volume
            << ") -> result=" << result << caller_suffix();
        logger::info(oss.str());
    }
    return result;
}

extern "C" FmodResult __stdcall hooked_channel_set_frequency(void* channel, float frequency) {
    const auto result = g_original_channel_set_frequency(channel, frequency);
    if (should_log_runtime_sample(g_channel_set_frequency_log_count)) {
        std::ostringstream oss;
        oss << "FMOD::Channel setFrequency(channel=" << channel << ", frequency=" << frequency
            << ") -> result=" << result << caller_suffix();
        logger::info(oss.str());
    }
    return result;
}

extern "C" FmodResult __stdcall hooked_channel_set_3d_min_max_distance(
    void* channel, float min_distance, float max_distance) {
    const auto result = g_original_channel_set_3d_min_max_distance(
        channel, min_distance, max_distance);
    if (should_log_runtime_sample(g_channel_set_3d_min_max_log_count)) {
        std::ostringstream oss;
        oss << "FMOD::Channel set3DMinMaxDistance(channel=" << channel << ", min=" << min_distance
            << ", max=" << max_distance << ") -> result=" << result << caller_suffix();
        logger::info(oss.str());
    }
    return result;
}

extern "C" FmodResult __stdcall hooked_channel_set_3d_attributes(
    void* channel, const void* position, const void* velocity) {
    const auto result = g_original_channel_set_3d_attributes(channel, position, velocity);
    if (should_log_runtime_sample(g_channel_set_3d_log_count)) {
        std::ostringstream oss;
        oss << "FMOD::Channel set3DAttributes(channel=" << channel << ", pos=" << position
            << ", vel=" << velocity << ") -> result=" << result << caller_suffix();
        logger::info(oss.str());
    }
    return result;
}

extern "C" FmodResult __stdcall hooked_event_set_3d_attributes(
    void* event_handle, const void* position, const void* velocity, const void* orientation) {
    const auto result =
        g_original_event_set_3d_attributes(event_handle, position, velocity, orientation);
    if (should_log_runtime_sample(g_event_set_3d_log_count)) {
        std::ostringstream oss;
        oss << "FMOD::Event set3DAttributes(event=" << event_handle << ", pos=" << position
            << ", vel=" << velocity << ", orientation=" << orientation << ") -> result="
            << result << caller_suffix();
        logger::info(oss.str());
    }
    return result;
}

extern "C" FmodResult __stdcall hooked_set_software_format(void* system, int sample_rate,
                                                           int sound_format, int output_channels,
                                                           int input_channels, int resampler) {
    const auto effective_sample_rate = static_cast<int>(
        resolve_sample_rate_hz(static_cast<std::uint32_t>(sample_rate), g_force_sample_rate_hz));
    const bool changed = effective_sample_rate != sample_rate;

    const auto result = g_original_set_software_format(
        system, effective_sample_rate, sound_format, output_channels, input_channels, resampler);

    if (g_log_audio_init_calls || changed) {
        std::ostringstream oss;
        oss << "FMOD setSoftwareFormat(rate=" << sample_rate;
        if (changed) {
            oss << "->" << effective_sample_rate;
        }
        oss << ", format=" << sound_format_name(sound_format) << '(' << sound_format
            << "), out_channels=" << output_channels << ", in_channels=" << input_channels
            << ", resampler=" << resampler << ") -> result=" << result << caller_suffix();
        logger::info(oss.str());
    }

    return result;
}

extern "C" FmodResult __stdcall hooked_set_dsp_buffer_size(void* system,
                                                           unsigned int buffer_length,
                                                           int buffer_count) {
    const auto effective_buffer_length =
        normalize_dsp_buffer_length(buffer_length, g_min_dsp_buffer_length);
    const auto effective_buffer_count = static_cast<int>(normalize_dsp_buffer_count(
        static_cast<std::uint32_t>((std::max)(0, buffer_count)), g_min_dsp_buffer_count));
    const bool changed =
        effective_buffer_length != buffer_length || effective_buffer_count != buffer_count;

    const auto result =
        g_original_set_dsp_buffer_size(system, effective_buffer_length, effective_buffer_count);

    if (g_log_audio_init_calls || changed) {
        std::ostringstream oss;
        oss << "FMOD setDSPBufferSize(length=" << buffer_length;
        if (effective_buffer_length != buffer_length) {
            oss << "->" << effective_buffer_length;
        }
        oss << ", count=" << buffer_count;
        if (effective_buffer_count != buffer_count) {
            oss << "->" << effective_buffer_count;
        }
        oss << ") -> result=" << result << caller_suffix();
        logger::info(oss.str());
    }

    return result;
}

extern "C" FmodResult __stdcall hooked_set_software_channels(void* system, int channel_count) {
    const auto result = g_original_set_software_channels(system, channel_count);

    if (g_log_audio_init_calls) {
        std::ostringstream oss;
        oss << "FMOD setSoftwareChannels(count=" << channel_count << ") -> result=" << result
            << caller_suffix();
        logger::info(oss.str());
    }

    return result;
}

bool install_direct_audio_init_patch(HMODULE game_module) {
    struct DirectPatchSite {
        const char* label;
        std::uintptr_t call_rva;
        std::uintptr_t expected_target_rva;
        void* replacement;
        void** original_target_out;
    };

    DirectPatchSite sites[] = {
        {"getVersion", kAudioInitGetVersionCallRva, kGetVersionThunkRva,
         reinterpret_cast<void*>(&hooked_get_version),
         reinterpret_cast<void**>(&g_original_get_version)},
        {"setSoftwareChannels", kAudioInitSetSoftwareChannelsCallRva, kSetSoftwareChannelsThunkRva,
         reinterpret_cast<void*>(&hooked_set_software_channels),
         reinterpret_cast<void**>(&g_original_set_software_channels)},
        {"getDriverCaps", kAudioInitGetDriverCapsCallRva, kGetDriverCapsThunkRva,
         reinterpret_cast<void*>(&hooked_get_driver_caps),
         reinterpret_cast<void**>(&g_original_get_driver_caps)},
        {"setDSPBufferSize", kAudioInitSetDspBufferSizeCallRva, kSetDspBufferSizeThunkRva,
         reinterpret_cast<void*>(&hooked_set_dsp_buffer_size),
         reinterpret_cast<void**>(&g_original_set_dsp_buffer_size)},
        {"setSpeakerMode", kAudioInitSetSpeakerModeCallRva, kSetSpeakerModeThunkRva,
         reinterpret_cast<void*>(&hooked_set_speaker_mode),
         reinterpret_cast<void**>(&g_original_set_speaker_mode)},
        {"setSpeakerModeRetry", kAudioInitSetSpeakerModeRetryCallRva, kSetSpeakerModeThunkRva,
         reinterpret_cast<void*>(&hooked_set_speaker_mode),
         reinterpret_cast<void**>(&g_original_set_speaker_mode)},
        {"setSoftwareFormat", kAudioInitSetSoftwareFormatCallRva, kSetSoftwareFormatThunkRva,
         reinterpret_cast<void*>(&hooked_set_software_format),
         reinterpret_cast<void**>(&g_original_set_software_format)},
    };

    for (const auto& site : sites) {
        if (!preflight_direct_patch_callsite(
                game_module, site.call_rva, site.expected_target_rva, site.label)) {
            return false;
        }
    }

    bool all_ok = true;
    for (const auto& site : sites) {
        void* original_target = nullptr;
        const auto ok = patch_relative_call(
            reinterpret_cast<std::uintptr_t>(game_module) + site.call_rva, site.replacement,
            &original_target);
        log_direct_patch_result(site.label, site.call_rva, ok);
        all_ok &= ok;
        if (ok && site.original_target_out != nullptr) {
            *site.original_target_out = original_target;
        }
    }

    if (!all_ok) {
        return false;
    }

    std::ostringstream oss;
    oss << "Installed direct Manhunt2 audio init patch"
        << " (force_sample_rate="
        << (g_force_sample_rate_hz == 0 ? std::string("off") : std::to_string(g_force_sample_rate_hz))
        << ", force_reported_version="
        << (g_force_reported_fmod_version == 0
                ? std::string("off")
                : format_fmod_version(g_force_reported_fmod_version))
        << ", min_dsp_buffer=" << g_min_dsp_buffer_length << 'x' << g_min_dsp_buffer_count
        << ", prefer_stereo_fallback=" << (g_prefer_stereo_fallback ? "on" : "off")
        << ", fix_missing_dsp_buffer=" << (g_fix_missing_dsp_buffer ? "on" : "off")
        << ", log_init=" << (g_log_audio_init_calls ? "on" : "off")
        << ", log_runtime=" << (g_log_audio_runtime_calls ? "on" : "off")
        << ", setter_elision=" << (g_enable_audio_setter_elision ? "on" : "off")
        << ", log_setter_elision=" << (g_log_audio_setter_elision ? "on" : "off")
        << ", runtime_sample_limit=" << g_runtime_log_sample_limit << ')';
    logger::info(oss.str());
    return true;
}

bool install_import_fmod_hooks(HMODULE game_module) {
    bool any_hook_installed = false;

    void* original_proc = nullptr;
    auto ok = iat_hook::patch_import(
        game_module, kFmodExModuleName, kGetDriverCapsProc,
        reinterpret_cast<void*>(&hooked_get_driver_caps), &original_proc);
    log_hook_result(kFmodExModuleName, kGetDriverCapsProc, ok);
    if (ok) {
        g_original_get_driver_caps = reinterpret_cast<GetDriverCapsFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodExModuleName, kSetSpeakerModeProc,
        reinterpret_cast<void*>(&hooked_set_speaker_mode), &original_proc);
    log_hook_result(kFmodExModuleName, kSetSpeakerModeProc, ok);
    if (ok) {
        g_original_set_speaker_mode = reinterpret_cast<SetSpeakerModeFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodExModuleName, kSetSoftwareFormatProc,
        reinterpret_cast<void*>(&hooked_set_software_format), &original_proc);
    log_hook_result(kFmodExModuleName, kSetSoftwareFormatProc, ok);
    if (ok) {
        g_original_set_software_format = reinterpret_cast<SetSoftwareFormatFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodExModuleName, kSetDspBufferSizeProc,
        reinterpret_cast<void*>(&hooked_set_dsp_buffer_size), &original_proc);
    log_hook_result(kFmodExModuleName, kSetDspBufferSizeProc, ok);
    if (ok) {
        g_original_set_dsp_buffer_size = reinterpret_cast<SetDspBufferSizeFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodExModuleName, kSetSoftwareChannelsProc,
        reinterpret_cast<void*>(&hooked_set_software_channels), &original_proc);
    log_hook_result(kFmodExModuleName, kSetSoftwareChannelsProc, ok);
    if (ok) {
        g_original_set_software_channels = reinterpret_cast<SetSoftwareChannelsFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodExModuleName, kGetVersionProc,
        reinterpret_cast<void*>(&hooked_get_version), &original_proc);
    log_hook_result(kFmodExModuleName, kGetVersionProc, ok);
    if (ok) {
        g_original_get_version = reinterpret_cast<GetVersionFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodExModuleName, kCreateStreamProc,
        reinterpret_cast<void*>(&hooked_create_stream), &original_proc);
    log_hook_result(kFmodExModuleName, kCreateStreamProc, ok);
    if (ok) {
        g_original_create_stream = reinterpret_cast<CreateStreamFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodExModuleName, kCreateSoundProc,
        reinterpret_cast<void*>(&hooked_create_sound), &original_proc);
    log_hook_result(kFmodExModuleName, kCreateSoundProc, ok);
    if (ok) {
        g_original_create_sound = reinterpret_cast<CreateSoundFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodExModuleName, kPlaySoundProc,
        reinterpret_cast<void*>(&hooked_play_sound), &original_proc);
    log_hook_result(kFmodExModuleName, kPlaySoundProc, ok);
    if (ok) {
        g_original_play_sound = reinterpret_cast<PlaySoundFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodExModuleName, kGetOpenStateProc,
        reinterpret_cast<void*>(&hooked_get_open_state), &original_proc);
    log_hook_result(kFmodExModuleName, kGetOpenStateProc, ok);
    if (ok) {
        g_original_get_open_state = reinterpret_cast<GetOpenStateFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodExModuleName, kSet3DListenerAttributesProc,
        reinterpret_cast<void*>(&hooked_set_3d_listener_attributes), &original_proc);
    log_hook_result(kFmodExModuleName, kSet3DListenerAttributesProc, ok);
    if (ok) {
        g_original_set_3d_listener_attributes =
            reinterpret_cast<Set3DListenerAttributesFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodExModuleName, kChannelStopProc,
        reinterpret_cast<void*>(&hooked_channel_stop), &original_proc);
    log_hook_result(kFmodExModuleName, kChannelStopProc, ok);
    if (ok) {
        g_original_channel_stop = reinterpret_cast<ChannelStopFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodExModuleName, kChannelSetPausedProc,
        reinterpret_cast<void*>(&hooked_channel_set_paused), &original_proc);
    log_hook_result(kFmodExModuleName, kChannelSetPausedProc, ok);
    if (ok) {
        g_original_channel_set_paused = reinterpret_cast<ChannelSetPausedFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodExModuleName, kChannelSetVolumeProc,
        reinterpret_cast<void*>(&hooked_channel_set_volume), &original_proc);
    log_hook_result(kFmodExModuleName, kChannelSetVolumeProc, ok);
    if (ok) {
        g_original_channel_set_volume = reinterpret_cast<ChannelSetVolumeFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodExModuleName, kChannelSetFrequencyProc,
        reinterpret_cast<void*>(&hooked_channel_set_frequency), &original_proc);
    log_hook_result(kFmodExModuleName, kChannelSetFrequencyProc, ok);
    if (ok) {
        g_original_channel_set_frequency = reinterpret_cast<ChannelSetFrequencyFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodExModuleName, kChannelSet3DMinMaxDistanceProc,
        reinterpret_cast<void*>(&hooked_channel_set_3d_min_max_distance), &original_proc);
    log_hook_result(kFmodExModuleName, kChannelSet3DMinMaxDistanceProc, ok);
    if (ok) {
        g_original_channel_set_3d_min_max_distance =
            reinterpret_cast<ChannelSet3DMinMaxDistanceFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodExModuleName, kChannelSet3DAttributesProc,
        reinterpret_cast<void*>(&hooked_channel_set_3d_attributes), &original_proc);
    log_hook_result(kFmodExModuleName, kChannelSet3DAttributesProc, ok);
    if (ok) {
        g_original_channel_set_3d_attributes =
            reinterpret_cast<ChannelSet3DAttributesFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodEventModuleName, kEventStartProc,
        reinterpret_cast<void*>(&hooked_event_start), &original_proc);
    log_hook_result(kFmodEventModuleName, kEventStartProc, ok);
    if (ok) {
        g_original_event_start = reinterpret_cast<EventStartFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodEventModuleName, kEventGetStateProc,
        reinterpret_cast<void*>(&hooked_event_get_state), &original_proc);
    log_hook_result(kFmodEventModuleName, kEventGetStateProc, ok);
    if (ok) {
        g_original_event_get_state = reinterpret_cast<EventGetStateFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodEventModuleName, kEventSetPausedProc,
        reinterpret_cast<void*>(&hooked_event_set_paused), &original_proc);
    log_hook_result(kFmodEventModuleName, kEventSetPausedProc, ok);
    if (ok) {
        g_original_event_set_paused = reinterpret_cast<EventSetPausedFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodEventModuleName, kEventSetVolumeProc,
        reinterpret_cast<void*>(&hooked_event_set_volume), &original_proc);
    log_hook_result(kFmodEventModuleName, kEventSetVolumeProc, ok);
    if (ok) {
        g_original_event_set_volume = reinterpret_cast<EventSetVolumeFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodEventModuleName, kEventSetPitchProc,
        reinterpret_cast<void*>(&hooked_event_set_pitch), &original_proc);
    log_hook_result(kFmodEventModuleName, kEventSetPitchProc, ok);
    if (ok) {
        g_original_event_set_pitch = reinterpret_cast<EventSetPitchFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodEventModuleName, kEventStopProc,
        reinterpret_cast<void*>(&hooked_event_stop), &original_proc);
    log_hook_result(kFmodEventModuleName, kEventStopProc, ok);
    if (ok) {
        g_original_event_stop = reinterpret_cast<EventStopFn>(original_proc);
        any_hook_installed = true;
    }

    original_proc = nullptr;
    ok = iat_hook::patch_import(
        game_module, kFmodEventModuleName, kEventSet3DAttributesProc,
        reinterpret_cast<void*>(&hooked_event_set_3d_attributes), &original_proc);
    log_hook_result(kFmodEventModuleName, kEventSet3DAttributesProc, ok);
    if (ok) {
        g_original_event_set_3d_attributes =
            reinterpret_cast<EventSet3DAttributesFn>(original_proc);
        any_hook_installed = true;
    }

    if (any_hook_installed) {
        std::ostringstream oss;
        oss << "Installed FMOD import hooks"
            << " (force_sample_rate="
            << (g_force_sample_rate_hz == 0 ? std::string("off")
                                            : std::to_string(g_force_sample_rate_hz))
            << ", force_reported_version="
            << (g_force_reported_fmod_version == 0
                    ? std::string("off")
                    : format_fmod_version(g_force_reported_fmod_version))
            << ", min_dsp_buffer=" << g_min_dsp_buffer_length << 'x' << g_min_dsp_buffer_count
            << ", prefer_stereo_fallback=" << (g_prefer_stereo_fallback ? "on" : "off")
            << ", fix_missing_dsp_buffer=" << (g_fix_missing_dsp_buffer ? "on" : "off")
            << ", log_init=" << (g_log_audio_init_calls ? "on" : "off")
            << ", log_runtime=" << (g_log_audio_runtime_calls ? "on" : "off")
            << ", setter_elision=" << (g_enable_audio_setter_elision ? "on" : "off")
            << ", log_setter_elision=" << (g_log_audio_setter_elision ? "on" : "off")
            << ", runtime_sample_limit=" << g_runtime_log_sample_limit << ')';
        logger::info(oss.str());
    }

    return any_hook_installed;
}

bool install_fmod_hooks(HMODULE game_module, bool enable_direct_audio_init_patch,
                        bool log_audio_init_calls, bool log_audio_runtime_calls,
                        bool enable_audio_setter_elision,
                        bool log_audio_setter_elision,
                        bool prefer_stereo_fallback, bool fix_missing_dsp_buffer,
                        std::uint32_t force_sample_rate_hz,
                        std::uint32_t force_reported_fmod_version,
                        std::uint32_t min_dsp_buffer_length,
                        std::uint32_t min_dsp_buffer_count,
                        std::uint32_t runtime_log_sample_limit) {
    g_log_audio_init_calls = log_audio_init_calls;
    g_log_audio_runtime_calls = log_audio_runtime_calls;
    g_enable_audio_setter_elision = enable_audio_setter_elision;
    g_log_audio_setter_elision = log_audio_setter_elision;
    g_prefer_stereo_fallback = prefer_stereo_fallback;
    g_fix_missing_dsp_buffer = fix_missing_dsp_buffer;
    g_last_driver_caps_forced_stereo = false;
    g_missing_dsp_buffer_fix_pending = false;
    g_force_sample_rate_hz = force_sample_rate_hz;
    g_force_reported_fmod_version = force_reported_fmod_version;
    g_min_dsp_buffer_length = min_dsp_buffer_length;
    g_min_dsp_buffer_count = min_dsp_buffer_count;
    g_runtime_log_sample_limit = runtime_log_sample_limit;
    g_game_module = game_module;
    g_game_module_size = module_size(game_module);
    g_original_set_software_format = nullptr;
    g_original_set_speaker_mode = nullptr;
    g_original_set_dsp_buffer_size = nullptr;
    g_original_get_driver_caps = nullptr;
    g_original_set_software_channels = nullptr;
    g_original_get_version = nullptr;
    g_original_create_stream = nullptr;
    g_original_create_sound = nullptr;
    g_original_play_sound = nullptr;
    g_original_get_open_state = nullptr;
    g_original_set_3d_listener_attributes = nullptr;
    g_original_event_start = nullptr;
    g_original_event_get_state = nullptr;
    g_original_event_set_paused = nullptr;
    g_original_event_set_volume = nullptr;
    g_original_event_set_pitch = nullptr;
    g_original_event_stop = nullptr;
    g_original_event_set_3d_attributes = nullptr;
    g_original_channel_stop = nullptr;
    g_original_channel_set_paused = nullptr;
    g_original_channel_set_volume = nullptr;
    g_original_channel_set_frequency = nullptr;
    g_original_channel_set_3d_min_max_distance = nullptr;
    g_original_channel_set_3d_attributes = nullptr;
    g_create_stream_log_count.store(0, std::memory_order_relaxed);
    g_create_sound_log_count.store(0, std::memory_order_relaxed);
    g_play_sound_log_count.store(0, std::memory_order_relaxed);
    g_get_open_state_log_count.store(0, std::memory_order_relaxed);
    g_set_3d_listener_log_count.store(0, std::memory_order_relaxed);
    g_event_start_log_count.store(0, std::memory_order_relaxed);
    g_event_get_state_log_count.store(0, std::memory_order_relaxed);
    g_event_set_paused_log_count.store(0, std::memory_order_relaxed);
    g_event_set_volume_log_count.store(0, std::memory_order_relaxed);
    g_event_set_pitch_log_count.store(0, std::memory_order_relaxed);
    g_event_stop_log_count.store(0, std::memory_order_relaxed);
    g_event_set_3d_log_count.store(0, std::memory_order_relaxed);
    g_channel_stop_log_count.store(0, std::memory_order_relaxed);
    g_channel_set_paused_log_count.store(0, std::memory_order_relaxed);
    g_channel_set_volume_log_count.store(0, std::memory_order_relaxed);
    g_channel_set_frequency_log_count.store(0, std::memory_order_relaxed);
    g_channel_set_3d_min_max_log_count.store(0, std::memory_order_relaxed);
    g_channel_set_3d_log_count.store(0, std::memory_order_relaxed);
    g_audio_setter_elision_log_count.store(0, std::memory_order_relaxed);
    clear_all_audio_setter_caches();

    const auto direct_patch_ok =
        enable_direct_audio_init_patch && install_direct_audio_init_patch(game_module);
    const auto import_hooks_ok = install_import_fmod_hooks(game_module);

    if (direct_patch_ok) {
        return true;
    }

    if (enable_direct_audio_init_patch) {
        logger::error("Direct audio init patch unavailable; falling back to FMOD import hooks");
    }
    return import_hooks_ok;
}

}  // namespace mh2modern::audio
