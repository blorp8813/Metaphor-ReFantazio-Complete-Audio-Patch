#include "stdafx.h"

#include "config.hpp"
#include "buffer_recovery.hpp"
#include "log.hpp"
#include "stall_detector.hpp"

#include <MinHook.h>

namespace {
constexpr std::wstring_view kFixName = L"MetaphorCompleteAudioPatch";
constexpr std::wstring_view kConfigName = L"MetaphorCompleteAudioPatch.ini";

constexpr std::wstring_view kXAudio27 = L"xaudio2_7.dll";
constexpr std::wstring_view kXAudio28 = L"xaudio2_8.dll";
constexpr std::wstring_view kXAudio29 = L"xaudio2_9.dll";
constexpr std::wstring_view kXAudio29Redist = L"xaudio2_9redist.dll";
constexpr std::wstring_view kOle32 = L"ole32.dll";

constexpr GUID kClsidMMDeviceEnumerator =
    {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
constexpr GUID kIidIMMDeviceEnumerator =
    {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
constexpr GUID kIidIAudioClient =
    {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
constexpr GUID kIidIAudioClient2 =
    {0x726778CD, 0xF60A, 0x4EDA, {0x82, 0xDE, 0xE4, 0x76, 0x10, 0xCD, 0x78, 0xAA}};
constexpr GUID kIidISpatialAudioClient =
    {0xBBF8E066, 0xAAAA, 0x49BE, {0x9A, 0x4D, 0xFD, 0x2A, 0x85, 0x8E, 0xA2, 0x7F}};
constexpr GUID kSubFormatIEEEFloat =
    {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

constexpr size_t kXAudio2ModernCreateMasteringVoiceIndex = 7;
constexpr size_t kXAudio27CreateMasteringVoiceIndex = 10;
constexpr size_t kIMMDeviceEnumeratorGetDefaultAudioEndpointIndex = 4;
constexpr size_t kIMMDeviceEnumeratorGetDeviceIndex = 5;
constexpr size_t kIMMDeviceActivateIndex = 3;
constexpr size_t kIAudioClientInitializeIndex = 3;
constexpr size_t kIAudioClientIsFormatSupportedIndex = 7;
constexpr size_t kIAudioClientGetMixFormatIndex = 8;
constexpr REFERENCE_TIME kSpatialDefaultPeriod = 100000;
constexpr DWORD kSpatialStreamFlags =
    AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;

HMODULE g_this_module = nullptr;
std::filesystem::path g_module_dir;
Config g_config{};

using XAudio2CreateFn = HRESULT(WINAPI*)(void** ppXAudio2, UINT32 flags, XAUDIO2_PROCESSOR processor);
using CreateMasteringVoiceModernFn =
    HRESULT(STDMETHODCALLTYPE*)(void* self, IXAudio2MasteringVoice** mastering_voice, UINT32 input_channels,
                                UINT32 input_sample_rate, UINT32 flags, LPCWSTR device_id,
                                const XAUDIO2_EFFECT_CHAIN* effect_chain, AUDIO_STREAM_CATEGORY stream_category);
using CreateMasteringVoice27Fn =
    HRESULT(STDMETHODCALLTYPE*)(void* self, IXAudio2MasteringVoice** mastering_voice, UINT32 input_channels,
                                UINT32 input_sample_rate, UINT32 flags, UINT32 device_index,
                                const XAUDIO2_EFFECT_CHAIN* effect_chain);
using CoCreateInstanceFn = HRESULT(WINAPI*)(REFCLSID clsid, LPUNKNOWN outer, DWORD clsctx, REFIID iid, LPVOID* out);
using GetDefaultAudioEndpointFn = HRESULT(STDMETHODCALLTYPE*)(void* self, EDataFlow flow, ERole role, IMMDevice** device);
using GetDeviceFn = HRESULT(STDMETHODCALLTYPE*)(void* self, LPCWSTR device_id, IMMDevice** device);
using IMMDeviceActivateFn = HRESULT(STDMETHODCALLTYPE*)(void* self, REFIID iid, DWORD clsctx, PROPVARIANT* activation_params, void** out);
using AudioClientInitializeFn =
    HRESULT(STDMETHODCALLTYPE*)(void* self, AUDCLNT_SHAREMODE share_mode, DWORD stream_flags,
                                REFERENCE_TIME hns_buffer_duration, REFERENCE_TIME hns_periodicity,
                                const WAVEFORMATEX* format, const GUID* audio_session_guid);
using AudioClientIsFormatSupportedFn =
    HRESULT(STDMETHODCALLTYPE*)(void* self, AUDCLNT_SHAREMODE share_mode, const WAVEFORMATEX* format, WAVEFORMATEX** closest_match);
using AudioClientGetMixFormatFn = HRESULT(STDMETHODCALLTYPE*)(void* self, WAVEFORMATEX** device_format);

XAudio2CreateFn g_xaudio2_create_modern = nullptr;
XAudio2CreateFn g_xaudio2_create_27 = nullptr;
CreateMasteringVoiceModernFn g_create_mastering_voice_modern = nullptr;
CreateMasteringVoice27Fn g_create_mastering_voice_27 = nullptr;
CoCreateInstanceFn g_co_create_instance = nullptr;
GetDefaultAudioEndpointFn g_get_default_audio_endpoint = nullptr;
GetDeviceFn g_get_device = nullptr;
IMMDeviceActivateFn g_immdevice_activate = nullptr;
AudioClientInitializeFn g_audio_client_initialize = nullptr;
AudioClientIsFormatSupportedFn g_audio_client_is_format_supported = nullptr;
AudioClientGetMixFormatFn g_audio_client_get_mix_format = nullptr;

std::atomic<bool> g_hooked_xaudio2_modern = false;
std::atomic<bool> g_hooked_xaudio2_27 = false;
std::atomic<bool> g_hooked_create_mastering_voice_modern = false;
std::atomic<bool> g_hooked_create_mastering_voice_27 = false;
std::atomic<bool> g_hooked_co_create_instance = false;
std::atomic<bool> g_hooked_get_default_audio_endpoint = false;
std::atomic<bool> g_hooked_get_device = false;
std::atomic<bool> g_hooked_immdevice_activate = false;
std::atomic<bool> g_hooked_audio_client_initialize = false;
std::atomic<bool> g_hooked_audio_client_is_format_supported = false;
std::atomic<bool> g_hooked_audio_client_get_mix_format = false;
std::atomic<bool> g_saw_wasapi_activity = false;
bool g_endpoint_registration_claimed = false;
SRWLOCK g_endpoint_registration_lock = SRWLOCK_INIT;
IMMDeviceEnumerator* g_retained_endpoint_enumerator = nullptr;
std::atomic<HANDLE> g_runtime_shutdown_event{nullptr};
HMODULE g_self_reference = nullptr;
std::atomic<std::uint64_t> g_active_spatial_streams{0};
std::atomic<bool> g_teardown_requested{false};
LARGE_INTEGER g_qpc_frequency{};

std::uint64_t QpcNow()
{
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<std::uint64_t>(value.QuadPart);
}

std::uint64_t MillisecondsToQpc(int milliseconds)
{
    return static_cast<std::uint64_t>(milliseconds) * static_cast<std::uint64_t>(g_qpc_frequency.QuadPart) / 1000ULL;
}

std::string Narrow(const std::wstring_view value)
{
    if (value.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }

    std::string utf8(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), utf8.data(), required, nullptr, nullptr);
    return utf8;
}

std::wstring GetModuleFileNameString(HMODULE module)
{
    std::wstring buffer(MAX_PATH, L'\0');

    while (true) {
        const DWORD copied = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (copied == 0) {
            return {};
        }

        if (copied < buffer.size() - 1) {
            buffer.resize(copied);
            return buffer;
        }

        buffer.resize(buffer.size() * 2);
    }
}

std::string GuidToString(REFGUID guid)
{
    wchar_t buffer[64]{};
    const int copied = StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer)));
    if (copied <= 1) {
        return "{}";
    }

    return Narrow(std::wstring_view(buffer, static_cast<size_t>(copied - 1)));
}

const char* ShareModeToString(AUDCLNT_SHAREMODE share_mode)
{
    switch (share_mode) {
    case AUDCLNT_SHAREMODE_SHARED:
        return "shared";
    case AUDCLNT_SHAREMODE_EXCLUSIVE:
        return "exclusive";
    default:
        return "unknown";
    }
}

bool IsAudioClientIid(REFIID iid)
{
    return IsEqualIID(iid, kIidIAudioClient) || IsEqualIID(iid, kIidIAudioClient2);
}

bool IsWaveFormatExtensible(const WAVEFORMATEX* format)
{
    return format && format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
           format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
}

bool ShouldForceStereoWaveFormat(const WAVEFORMATEX* format)
{
    return format && format->nChannels > 2;
}

void ForceStereoWaveFormatInPlace(WAVEFORMATEX* format)
{
    if (!format || format->nChannels <= 2) {
        return;
    }

    format->nChannels = 2;
    if (format->wBitsPerSample != 0) {
        format->nBlockAlign = static_cast<WORD>((format->nChannels * format->wBitsPerSample) / 8);
    }
    if (format->nBlockAlign != 0) {
        format->nAvgBytesPerSec = format->nSamplesPerSec * format->nBlockAlign;
    }

    if (IsWaveFormatExtensible(format)) {
        auto* extensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format);
        extensible->dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    }
}

std::vector<std::uint8_t> CloneWaveFormat(const WAVEFORMATEX* format)
{
    const size_t size = sizeof(WAVEFORMATEX) + format->cbSize;
    std::vector<std::uint8_t> copy(size);
    std::memcpy(copy.data(), format, size);
    return copy;
}

std::string DescribeWaveFormat(const WAVEFORMATEX* format)
{
    if (!format) {
        return "<null>";
    }

    std::ostringstream stream;
    stream << "tag=0x" << std::hex << format->wFormatTag << std::dec
           << " channels=" << format->nChannels
           << " sample_rate=" << format->nSamplesPerSec
           << " bits=" << format->wBitsPerSample
           << " block_align=" << format->nBlockAlign
           << " avg_bytes=" << format->nAvgBytesPerSec
           << " cbSize=" << format->cbSize;

    if (IsWaveFormatExtensible(format)) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        stream << " mask=0x" << std::hex << extensible->dwChannelMask << std::dec
               << " subformat=" << GuidToString(extensible->SubFormat);
    }

    return stream.str();
}

bool ShouldOverrideInputChannels(UINT32 input_channels)
{
    if (!g_config.force_stereo_mastering_voice) {
        return false;
    }

    if (input_channels == 0) {
        return true;
    }

    if (input_channels <= 2) {
        return false;
    }

    return g_config.override_explicit_multichannel_voices;
}

void LogMasteringVoiceResult(HRESULT result, IXAudio2MasteringVoice* mastering_voice)
{
    if (!mastering_voice) {
        Log::Warn("CreateMasteringVoice returned 0x%08lX without a mastering voice pointer", static_cast<unsigned long>(result));
        return;
    }

    XAUDIO2_VOICE_DETAILS details{};
    mastering_voice->GetVoiceDetails(&details);

    DWORD channel_mask = 0;
    mastering_voice->GetChannelMask(&channel_mask);

    Log::Info(
        "CreateMasteringVoice result=0x%08lX actual_channels=%u channel_mask=0x%08lX",
        static_cast<unsigned long>(result),
        details.InputChannels,
        static_cast<unsigned long>(channel_mask)
    );
}

HRESULT STDMETHODCALLTYPE HookedCreateMasteringVoiceModern(
    void* self,
    IXAudio2MasteringVoice** mastering_voice,
    UINT32 input_channels,
    UINT32 input_sample_rate,
    UINT32 flags,
    LPCWSTR device_id,
    const XAUDIO2_EFFECT_CHAIN* effect_chain,
    AUDIO_STREAM_CATEGORY stream_category)
{
    const UINT32 original_channels = input_channels;
    if (ShouldOverrideInputChannels(input_channels)) {
        input_channels = 2;
        Log::Warn("Overriding modern mastering voice channels from %u to %u", original_channels, input_channels);
    } else {
        Log::Info("Leaving modern mastering voice channels unchanged at %u", input_channels);
    }

    if (device_id) {
        Log::Info("Modern mastering voice device=%s sample_rate=%u flags=0x%08lX stream_category=%d",
                  Narrow(std::wstring_view(device_id)).c_str(),
                  input_sample_rate,
                  static_cast<unsigned long>(flags),
                  static_cast<int>(stream_category));
    } else {
        Log::Info("Modern mastering voice using default device sample_rate=%u flags=0x%08lX stream_category=%d",
                  input_sample_rate,
                  static_cast<unsigned long>(flags),
                  static_cast<int>(stream_category));
    }

    const HRESULT result = g_create_mastering_voice_modern(
        self,
        mastering_voice,
        input_channels,
        input_sample_rate,
        flags,
        device_id,
        effect_chain,
        stream_category
    );

    LogMasteringVoiceResult(result, mastering_voice ? *mastering_voice : nullptr);
    return result;
}

HRESULT STDMETHODCALLTYPE HookedCreateMasteringVoice27(
    void* self,
    IXAudio2MasteringVoice** mastering_voice,
    UINT32 input_channels,
    UINT32 input_sample_rate,
    UINT32 flags,
    UINT32 device_index,
    const XAUDIO2_EFFECT_CHAIN* effect_chain)
{
    const UINT32 original_channels = input_channels;
    if (ShouldOverrideInputChannels(input_channels)) {
        input_channels = 2;
        Log::Warn("Overriding XAudio2 2.7 mastering voice channels from %u to %u", original_channels, input_channels);
    } else {
        Log::Info("Leaving XAudio2 2.7 mastering voice channels unchanged at %u", input_channels);
    }

    Log::Info("XAudio2 2.7 mastering voice device_index=%u sample_rate=%u flags=0x%08lX",
              device_index,
              input_sample_rate,
              static_cast<unsigned long>(flags));

    const HRESULT result = g_create_mastering_voice_27(
        self,
        mastering_voice,
        input_channels,
        input_sample_rate,
        flags,
        device_index,
        effect_chain
    );

    LogMasteringVoiceResult(result, mastering_voice ? *mastering_voice : nullptr);
    return result;
}

void HookModernCreateMasteringVoice(void* xaudio2_instance, const wchar_t* module_name)
{
    if (g_hooked_create_mastering_voice_modern.load() || !xaudio2_instance) {
        return;
    }

    auto** vtable = *reinterpret_cast<void***>(xaudio2_instance);
    if (!vtable) {
        Log::Error("Failed to inspect IXAudio2 vtable for %s", Narrow(module_name).c_str());
        return;
    }

    void* target = vtable[kXAudio2ModernCreateMasteringVoiceIndex];
    if (!target) {
        Log::Error("Modern CreateMasteringVoice vtable slot was null for %s", Narrow(module_name).c_str());
        return;
    }

    const MH_STATUS create_status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedCreateMasteringVoiceModern),
                                                  reinterpret_cast<void**>(&g_create_mastering_voice_modern));
    if (create_status != MH_OK) {
        Log::Error("MH_CreateHook failed for modern CreateMasteringVoice in %s: %d",
                   Narrow(module_name).c_str(),
                   static_cast<int>(create_status));
        return;
    }

    const MH_STATUS enable_status = MH_EnableHook(target);
    if (enable_status != MH_OK && enable_status != MH_ERROR_ENABLED) {
        Log::Error("MH_EnableHook failed for modern CreateMasteringVoice in %s: %d",
                   Narrow(module_name).c_str(),
                   static_cast<int>(enable_status));
        return;
    }

    g_hooked_create_mastering_voice_modern = true;
    Log::Info("Hooked modern CreateMasteringVoice in %s", Narrow(module_name).c_str());
}

void Hook27CreateMasteringVoice(void* xaudio2_instance, const wchar_t* module_name)
{
    if (g_hooked_create_mastering_voice_27.load() || !xaudio2_instance) {
        return;
    }

    auto** vtable = *reinterpret_cast<void***>(xaudio2_instance);
    if (!vtable) {
        Log::Error("Failed to inspect IXAudio2 2.7 vtable for %s", Narrow(module_name).c_str());
        return;
    }

    void* target = vtable[kXAudio27CreateMasteringVoiceIndex];
    if (!target) {
        Log::Error("XAudio2 2.7 CreateMasteringVoice vtable slot was null for %s", Narrow(module_name).c_str());
        return;
    }

    const MH_STATUS create_status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedCreateMasteringVoice27),
                                                  reinterpret_cast<void**>(&g_create_mastering_voice_27));
    if (create_status != MH_OK) {
        Log::Error("MH_CreateHook failed for XAudio2 2.7 CreateMasteringVoice in %s: %d",
                   Narrow(module_name).c_str(),
                   static_cast<int>(create_status));
        return;
    }

    const MH_STATUS enable_status = MH_EnableHook(target);
    if (enable_status != MH_OK && enable_status != MH_ERROR_ENABLED) {
        Log::Error("MH_EnableHook failed for XAudio2 2.7 CreateMasteringVoice in %s: %d",
                   Narrow(module_name).c_str(),
                   static_cast<int>(enable_status));
        return;
    }

    g_hooked_create_mastering_voice_27 = true;
    Log::Info("Hooked XAudio2 2.7 CreateMasteringVoice in %s", Narrow(module_name).c_str());
}

HRESULT WINAPI HookedXAudio2CreateModern(void** xaudio2, UINT32 flags, XAUDIO2_PROCESSOR processor)
{
    const HRESULT result = g_xaudio2_create_modern(xaudio2, flags, processor);
    Log::Info("XAudio2Create modern result=0x%08lX flags=0x%08lX processor=0x%08lX instance=%p",
              static_cast<unsigned long>(result),
              static_cast<unsigned long>(flags),
              static_cast<unsigned long>(processor),
              xaudio2 ? *xaudio2 : nullptr);

    if (SUCCEEDED(result) && xaudio2 && *xaudio2) {
        HookModernCreateMasteringVoice(*xaudio2, L"modern XAudio2");
    }

    return result;
}

HRESULT WINAPI HookedXAudio2Create27(void** xaudio2, UINT32 flags, XAUDIO2_PROCESSOR processor)
{
    const HRESULT result = g_xaudio2_create_27(xaudio2, flags, processor);
    Log::Info("XAudio2Create 2.7 result=0x%08lX flags=0x%08lX processor=0x%08lX instance=%p",
              static_cast<unsigned long>(result),
              static_cast<unsigned long>(flags),
              static_cast<unsigned long>(processor),
              xaudio2 ? *xaudio2 : nullptr);

    if (SUCCEEDED(result) && xaudio2 && *xaudio2) {
        Hook27CreateMasteringVoice(*xaudio2, kXAudio27.data());
    }

    return result;
}

void HookXAudio2Export(const wchar_t* module_name, bool is_xaudio27)
{
    HMODULE module = GetModuleHandleW(module_name);
    if (!module) {
        return;
    }

    if (is_xaudio27 && g_hooked_xaudio2_27.load()) {
        return;
    }
    if (!is_xaudio27 && g_hooked_xaudio2_modern.load()) {
        return;
    }

    auto* target = reinterpret_cast<void*>(GetProcAddress(module, "XAudio2Create"));
    if (!target) {
        Log::Warn("Module %s was loaded but XAudio2Create was not exported", Narrow(module_name).c_str());
        return;
    }

    void* original = nullptr;
    const MH_STATUS create_status = MH_CreateHook(
        target,
        is_xaudio27 ? reinterpret_cast<void*>(&HookedXAudio2Create27) : reinterpret_cast<void*>(&HookedXAudio2CreateModern),
        &original
    );

    if (create_status != MH_OK) {
        Log::Error("MH_CreateHook failed for XAudio2Create in %s: %d",
                   Narrow(module_name).c_str(),
                   static_cast<int>(create_status));
        return;
    }

    const MH_STATUS enable_status = MH_EnableHook(target);
    if (enable_status != MH_OK && enable_status != MH_ERROR_ENABLED) {
        Log::Error("MH_EnableHook failed for XAudio2Create in %s: %d",
                   Narrow(module_name).c_str(),
                   static_cast<int>(enable_status));
        return;
    }

    if (is_xaudio27) {
        g_xaudio2_create_27 = reinterpret_cast<XAudio2CreateFn>(original);
        g_hooked_xaudio2_27 = true;
    } else {
        g_xaudio2_create_modern = reinterpret_cast<XAudio2CreateFn>(original);
        g_hooked_xaudio2_modern = true;
    }

    Log::Info("Hooked XAudio2Create in %s", Narrow(module_name).c_str());
}

void HookAudioClient(void* audio_client);
struct StereoMixCoefficients {
    float left = 0.0f;
    float right = 0.0f;
};

constexpr UINT32 kSpatialSupportedStaticMaskBits =
    AudioObjectType_FrontLeft |
    AudioObjectType_FrontRight |
    AudioObjectType_FrontCenter |
    AudioObjectType_LowFrequency |
    AudioObjectType_BackLeft |
    AudioObjectType_BackRight |
    AudioObjectType_SideLeft |
    AudioObjectType_SideRight |
    AudioObjectType_TopFrontLeft |
    AudioObjectType_TopFrontRight |
    AudioObjectType_TopBackLeft |
    AudioObjectType_TopBackRight;

bool IsFloatObjectFormat(const WAVEFORMATEX* format)
{
    if (!format || format->nChannels != 1 || format->nSamplesPerSec != 48000 || format->wBitsPerSample != 32) {
        return false;
    }

    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return true;
    }

    if (IsWaveFormatExtensible(format)) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return IsEqualGUID(extensible->SubFormat, kSubFormatIEEEFloat);
    }

    return false;
}

bool WaveFormatsEqual(const WAVEFORMATEX* lhs, const WAVEFORMATEX* rhs)
{
    if (!lhs || !rhs || lhs->cbSize != rhs->cbSize) {
        return false;
    }

    return std::memcmp(lhs, rhs, sizeof(WAVEFORMATEX) + lhs->cbSize) == 0;
}

WAVEFORMATEX* AllocateWaveFormatCopy(const WAVEFORMATEX* format)
{
    if (!format) {
        return nullptr;
    }

    const size_t size = sizeof(WAVEFORMATEX) + format->cbSize;
    auto* copy = static_cast<WAVEFORMATEX*>(std::malloc(size));
    if (!copy) {
        return nullptr;
    }

    std::memcpy(copy, format, size);
    return copy;
}

StereoMixCoefficients GetStereoMixCoefficients(AudioObjectType type)
{
    switch (type) {
    case AudioObjectType_FrontLeft:
        return {1.00f, 0.00f};
    case AudioObjectType_FrontRight:
        return {0.00f, 1.00f};
    case AudioObjectType_FrontCenter:
        return {0.85f, 0.85f};
    case AudioObjectType_LowFrequency:
        return {0.30f, 0.30f};
    case AudioObjectType_BackLeft:
        return {0.55f, 0.00f};
    case AudioObjectType_BackRight:
        return {0.00f, 0.55f};
    case AudioObjectType_SideLeft:
        return {0.65f, 0.00f};
    case AudioObjectType_SideRight:
        return {0.00f, 0.65f};
    case AudioObjectType_TopFrontLeft:
        return {0.45f, 0.00f};
    case AudioObjectType_TopFrontRight:
        return {0.00f, 0.45f};
    case AudioObjectType_TopBackLeft:
        return {0.30f, 0.00f};
    case AudioObjectType_TopBackRight:
        return {0.00f, 0.30f};
    case AudioObjectType_BackCenter:
        return {0.40f, 0.40f};
    default:
        return {};
    }
}

void BuildStereoFloatFormat(WAVEFORMATEXTENSIBLE& format, UINT32 sample_rate)
{
    std::memset(&format, 0, sizeof(format));
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = 2;
    format.Format.nSamplesPerSec = sample_rate;
    format.Format.wBitsPerSample = 32;
    format.Format.nBlockAlign = static_cast<WORD>((format.Format.nChannels * format.Format.wBitsPerSample) / 8);
    format.Format.nAvgBytesPerSec = format.Format.nSamplesPerSec * format.Format.nBlockAlign;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = format.Format.wBitsPerSample;
    format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    format.SubFormat = kSubFormatIEEEFloat;
}

class SpatialAudioWrapper;

class AudioClientRecoveryBackend {
public:
    AudioClientRecoveryBackend(IAudioClient* audio_client,
                               IAudioRenderClient* render_client,
                               std::atomic<bool>& started,
                               void* stream)
        : audio_client_(audio_client), render_client_(render_client), started_(started), stream_(stream)
    {
    }

    BufferRecovery::Hr GetBuffer(std::uint32_t frames, void** buffer)
    {
        return static_cast<BufferRecovery::Hr>(
            render_client_->GetBuffer(frames, reinterpret_cast<BYTE**>(buffer)));
    }

    BufferRecovery::Hr GetCurrentPadding(std::uint32_t* padding)
    {
        return static_cast<BufferRecovery::Hr>(audio_client_->GetCurrentPadding(padding));
    }

    BufferRecovery::Hr Stop()
    {
        const HRESULT hr = audio_client_->Stop();
        if (SUCCEEDED(hr)) {
            started_.store(false, std::memory_order_release);
        }
        return static_cast<BufferRecovery::Hr>(hr);
    }

    BufferRecovery::Hr Reset()
    {
        const HRESULT hr = audio_client_->Reset();
        if (SUCCEEDED(hr)) {
            started_.store(false, std::memory_order_release);
        }
        return static_cast<BufferRecovery::Hr>(hr);
    }

    BufferRecovery::Hr Start()
    {
        const HRESULT hr = audio_client_->Start();
        const bool previous_started = started_.load(std::memory_order_acquire);
        started_.store(StartedStateAfterStartResult(previous_started, static_cast<std::int32_t>(hr)),
                       std::memory_order_release);
        return static_cast<BufferRecovery::Hr>(hr);
    }

    std::uint64_t NowTicks() const { return QpcNow(); }
    bool IsStarted() const { return started_.load(std::memory_order_acquire); }

    void StageBefore(BufferRecovery::Stage stage)
    {
        if (stage == BufferRecovery::Stage::Stop) {
            Log::Warn("BUFFER_RECOVERY_STARTED stream=%p started=%d",
                      stream_, started_.load(std::memory_order_acquire));
        }
        Log::Warn("BUFFER_RECOVERY_STAGE_BEFORE stream=%p stage=%s started=%d",
                  stream_, StageName(stage), started_.load(std::memory_order_acquire));
    }

    void StageAfter(BufferRecovery::Stage stage, BufferRecovery::Hr result)
    {
        const bool started = started_.load(std::memory_order_acquire);
        Log::Warn("BUFFER_RECOVERY_STAGE_AFTER stream=%p stage=%s result=0x%08lX started=%d",
                  stream_, StageName(stage), static_cast<unsigned long>(result), started);
        if (stage == BufferRecovery::Stage::Reset && BufferRecovery::Succeeded(result)) {
            Log::Warn("BUFFER_RECOVERY_RESET_SUCCEEDED stream=%p reset_hr=0x%08lX started_after_reset=%d",
                      stream_, static_cast<unsigned long>(result), started);
        }
    }

private:
    static const char* StageName(BufferRecovery::Stage stage)
    {
        switch (stage) {
        case BufferRecovery::Stage::Stop:
            return "Stop";
        case BufferRecovery::Stage::Reset:
            return "Reset";
        case BufferRecovery::Stage::Start:
            return "Start";
        case BufferRecovery::Stage::GetCurrentPadding:
            return "GetCurrentPadding";
        case BufferRecovery::Stage::GetBuffer:
            return "GetBuffer";
        }
        return "Unknown";
    }

    IAudioClient* audio_client_ = nullptr;
    IAudioRenderClient* render_client_ = nullptr;
    std::atomic<bool>& started_;
    void* stream_ = nullptr;
};

class SpatialRenderObject final : public ISpatialAudioObject {
public:
    SpatialRenderObject(class SpatialRenderStream* stream, AudioObjectType type, UINT32 frames);
    ~SpatialRenderObject();
    void DetachStream();
    const std::vector<float>& Samples() const { return buffer_; }
    std::vector<float>& Samples() { return buffer_; }
    AudioObjectType Type() const { return type_; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetBuffer(BYTE** buffer, UINT32* bytes) override;
    HRESULT STDMETHODCALLTYPE SetEndOfStream(UINT32) override;
    HRESULT STDMETHODCALLTYPE IsActive(BOOL* active) override;
    HRESULT STDMETHODCALLTYPE GetAudioObjectType(AudioObjectType* type) override;
    HRESULT STDMETHODCALLTYPE SetPosition(float, float, float) override;
    HRESULT STDMETHODCALLTYPE SetVolume(float) override;

    std::atomic<ULONG> ref_{1};
    class SpatialRenderStream* stream_ = nullptr;
    AudioObjectType type_ = AudioObjectType_None;
    std::vector<float> buffer_;
};

class SpatialRenderStream final : public ISpatialAudioObjectRenderStream {
public:
    SpatialRenderStream(SpatialAudioWrapper* owner, const SpatialAudioObjectRenderStreamActivationParams& params);
    ~SpatialRenderStream();
    void RemoveObject(SpatialRenderObject* object);
    HRESULT ActivateOutput();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetAvailableDynamicObjectCount(UINT32* count) override;
    HRESULT STDMETHODCALLTYPE GetService(REFIID riid, void** service) override;
    HRESULT STDMETHODCALLTYPE Start() override;
    HRESULT STDMETHODCALLTYPE Stop() override;
    HRESULT STDMETHODCALLTYPE Reset() override;
    HRESULT STDMETHODCALLTYPE BeginUpdatingAudioObjects(UINT32* dynamic_count, UINT32* frames) override;
    HRESULT STDMETHODCALLTYPE EndUpdatingAudioObjects() override;
    HRESULT STDMETHODCALLTYPE ActivateSpatialAudioObject(AudioObjectType type, ISpatialAudioObject** out) override;
    bool MixToStereo();
    void LogBufferRecoveryOutcome(const BufferRecovery::AcquireOutcome& outcome);
    void StartWatchdog();
    void StopWatchdog();
    static DWORD WINAPI WatchdogThread(void* context);
    void WatchdogLoop();

    std::atomic<ULONG> ref_{1};
    CRITICAL_SECTION lock_{};
    SpatialAudioWrapper* owner_ = nullptr;
    IAudioClient* audio_client_ = nullptr;
    IAudioRenderClient* render_client_ = nullptr;
    IAudioClock* audio_clock_ = nullptr;
    ISpatialAudioObjectRenderStreamNotify* notify_ = nullptr;
    HANDLE event_handle_ = nullptr;
    AudioObjectType static_mask_ = AudioObjectType_None;
    UINT32 period_frames_ = 0;
    UINT32 buffer_capacity_frames_ = 0;
    UINT32 update_frames_ = ~0u;
    float* render_buffer_ = nullptr;
    WAVEFORMATEX* object_format_ = nullptr;
    WAVEFORMATEXTENSIBLE output_format_{};
    std::list<SpatialRenderObject*> objects_;
    HANDLE watchdog_stop_event_ = nullptr;
    HANDLE watchdog_thread_ = nullptr;
    std::atomic<bool> started_{false};
    std::atomic<std::uint64_t> last_non_silent_qpc_{0};
    std::atomic<std::uint64_t> last_callback_qpc_{0};
    std::atomic<std::uint64_t> get_buffer_count_{0};
    std::atomic<std::uint64_t> release_buffer_count_{0};
    std::atomic<std::uint64_t> total_submitted_frames_{0};
    std::atomic<std::uint64_t> get_buffer_failures_{0};
    std::atomic<std::uint64_t> release_buffer_failures_{0};
    std::atomic<std::uint64_t> successful_get_buffer_cycles_{0};
    std::atomic<long> last_get_buffer_hr_{S_OK};
    std::atomic<long> last_release_buffer_hr_{S_OK};
    bool recovery_in_progress_ = false;
    BufferRecovery::Coordinator recovery_;

    friend class SpatialRenderObject;
};

class SpatialAudioWrapper final : public ISpatialAudioClient, public IAudioFormatEnumerator {
public:
    SpatialAudioWrapper(IMMDevice* device, ISpatialAudioClient* passthrough);
    ~SpatialAudioWrapper();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE GetStaticObjectPosition(AudioObjectType, float* x, float* y, float* z) override;
    HRESULT STDMETHODCALLTYPE GetNativeStaticObjectTypeMask(AudioObjectType* mask) override;
    HRESULT STDMETHODCALLTYPE GetMaxDynamicObjectCount(UINT32* value) override;
    HRESULT STDMETHODCALLTYPE GetSupportedAudioObjectFormatEnumerator(IAudioFormatEnumerator** enumerator) override;
    HRESULT STDMETHODCALLTYPE GetMaxFrameCount(const WAVEFORMATEX* format, UINT32* count) override;
    HRESULT STDMETHODCALLTYPE IsAudioObjectFormatSupported(const WAVEFORMATEX* format) override;
    HRESULT STDMETHODCALLTYPE IsSpatialAudioStreamAvailable(REFIID stream_uuid, const PROPVARIANT* info) override;
    HRESULT STDMETHODCALLTYPE ActivateSpatialAudioStream(const PROPVARIANT* prop, REFIID riid, void** out) override;

    HRESULT STDMETHODCALLTYPE GetCount(UINT32* count) override;
    HRESULT STDMETHODCALLTYPE GetFormat(UINT32 index, WAVEFORMATEX** format) override;

    IMMDevice* Device() const { return device_; }
    ISpatialAudioClient* Passthrough() const { return passthrough_; }
    const WAVEFORMATEX* ObjectFormat() const { return &object_format_; }

    std::atomic<ULONG> ref_{1};
    IMMDevice* device_ = nullptr;
    ISpatialAudioClient* passthrough_ = nullptr;
    WAVEFORMATEX object_format_{};
};

SpatialRenderObject::SpatialRenderObject(SpatialRenderStream* stream, AudioObjectType type, UINT32 frames)
    : stream_(stream), type_(type), buffer_(frames, 0.0f)
{
    if (stream_) {
        stream_->AddRef();
    }
}

void SpatialRenderObject::DetachStream()
{
    stream_ = nullptr;
}

SpatialRenderObject::~SpatialRenderObject()
{
    if (stream_) {
        stream_->RemoveObject(this);
        stream_->Release();
    }
}

HRESULT STDMETHODCALLTYPE SpatialRenderObject::QueryInterface(REFIID riid, void** out)
{
    if (!out) {
        return E_POINTER;
    }

    *out = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ISpatialAudioObjectBase) ||
        IsEqualIID(riid, IID_ISpatialAudioObject)) {
        *out = static_cast<ISpatialAudioObject*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE SpatialRenderObject::AddRef()
{
    return ref_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE SpatialRenderObject::Release()
{
    const ULONG ref = ref_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (ref == 0) {
        delete this;
    }
    return ref;
}

HRESULT STDMETHODCALLTYPE SpatialRenderObject::GetBuffer(BYTE** buffer, UINT32* bytes)
{
    if (!buffer || !bytes || !stream_) {
        return E_POINTER;
    }

    EnterCriticalSection(&stream_->lock_);
    if (stream_->update_frames_ == ~0u) {
        LeaveCriticalSection(&stream_->lock_);
        return SPTLAUDCLNT_E_OUT_OF_ORDER;
    }

    *buffer = reinterpret_cast<BYTE*>(buffer_.data());
    *bytes = stream_->update_frames_ * static_cast<UINT32>(sizeof(float));
    LeaveCriticalSection(&stream_->lock_);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialRenderObject::SetEndOfStream(UINT32)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialRenderObject::IsActive(BOOL* active)
{
    if (!active) {
        return E_POINTER;
    }
    *active = TRUE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialRenderObject::GetAudioObjectType(AudioObjectType* type)
{
    if (!type) {
        return E_POINTER;
    }
    *type = type_;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialRenderObject::SetPosition(float, float, float)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialRenderObject::SetVolume(float)
{
    return S_OK;
}

SpatialRenderStream::SpatialRenderStream(SpatialAudioWrapper* owner, const SpatialAudioObjectRenderStreamActivationParams& params)
    : owner_(owner),
      notify_(params.NotifyObject),
      event_handle_(params.EventHandle),
      static_mask_(params.StaticObjectTypeMask),
      object_format_(AllocateWaveFormatCopy(params.ObjectFormat)),
      recovery_({
          g_config.recovery_enabled,
          g_config.recovery_adaptive_buffer_retry,
          g_config.recovery_reset_restart_fallback,
          static_cast<std::uint32_t>(g_config.recovery_maximum_attempts_per_failure),
          static_cast<std::uint32_t>(g_config.recovery_maximum_recoveries_per_window),
          MillisecondsToQpc(g_config.recovery_window_ms),
          MillisecondsToQpc(g_config.recovery_cooldown_ms),
          static_cast<std::uint64_t>(g_config.recovery_fault_inject_buffer_too_large_after),
      })
{
    InitializeCriticalSection(&lock_);
    g_active_spatial_streams.fetch_add(1, std::memory_order_relaxed);
    Log::Info("SpatialRenderStream created stream=%p event_handle=%p notify=%p static_mask=0x%08lX object_format=%s",
              this,
              event_handle_,
              notify_,
              static_cast<unsigned long>(static_mask_),
              DescribeWaveFormat(object_format_).c_str());
    if (owner_) {
        owner_->AddRef();
    }
    if (notify_) {
        notify_->AddRef();
    }
}

SpatialRenderStream::~SpatialRenderStream()
{
    Log::Info("SpatialRenderStream destroying stream=%p", this);
    StopWatchdog();
    if (audio_client_) {
        const HRESULT hr = audio_client_->Stop();
        Log::Info("IAudioClient::Stop stream=%p reason=destructor result=0x%08lX", this, static_cast<unsigned long>(hr));
    }
    if (render_client_ && update_frames_ != ~0u && update_frames_ > 0) {
        const HRESULT hr = render_client_->ReleaseBuffer(update_frames_, 0);
        Log::Info("IAudioRenderClient::ReleaseBuffer stream=%p reason=destructor frames=%u flags=0 result=0x%08lX",
                  this, update_frames_, static_cast<unsigned long>(hr));
    }

    for (SpatialRenderObject* object : objects_) {
        object->DetachStream();
        delete object;
    }
    objects_.clear();

    if (notify_) {
        notify_->Release();
    }
    if (render_client_) {
        render_client_->Release();
    }
    if (audio_clock_) {
        audio_clock_->Release();
    }
    if (audio_client_) {
        audio_client_->Release();
    }
    if (owner_) {
        owner_->Release();
    }
    std::free(object_format_);
    DeleteCriticalSection(&lock_);
    g_active_spatial_streams.fetch_sub(1, std::memory_order_release);
}

void SpatialRenderStream::RemoveObject(SpatialRenderObject* object)
{
    EnterCriticalSection(&lock_);
    objects_.remove(object);
    LeaveCriticalSection(&lock_);
}

HRESULT SpatialRenderStream::ActivateOutput()
{
    if (!owner_ || !owner_->Device() || !g_immdevice_activate || !event_handle_) {
        Log::Error("Spatial output activation precondition failed stream=%p owner=%p device=%p activate=%p event=%p",
                   this, owner_, owner_ ? owner_->Device() : nullptr, g_immdevice_activate, event_handle_);
        return E_FAIL;
    }

    void* raw_audio_client = nullptr;
    HRESULT hr = g_immdevice_activate(owner_->Device(), kIidIAudioClient, CLSCTX_INPROC_SERVER, nullptr, &raw_audio_client);
    Log::Info("IMMDevice::Activate internal IAudioClient stream=%p result=0x%08lX client=%p",
              this, static_cast<unsigned long>(hr), raw_audio_client);
    if (FAILED(hr) || !raw_audio_client) {
        Log::Error("IMMDevice::Activate internal IAudioClient failed stream=%p result=0x%08lX client=%p",
                   this, static_cast<unsigned long>(hr), raw_audio_client);
        return FAILED(hr) ? hr : E_FAIL;
    }

    audio_client_ = static_cast<IAudioClient*>(raw_audio_client);

    REFERENCE_TIME default_period = 0;
    hr = audio_client_->GetDevicePeriod(&default_period, nullptr);
    Log::Info("IAudioClient::GetDevicePeriod stream=%p result=0x%08lX default_period=%lld",
              this, static_cast<unsigned long>(hr), static_cast<long long>(default_period));
    if (FAILED(hr) || default_period <= 0) {
        Log::Warn("IAudioClient::GetDevicePeriod fallback stream=%p result=0x%08lX fallback=%lld",
                  this, static_cast<unsigned long>(hr), static_cast<long long>(kSpatialDefaultPeriod));
        default_period = kSpatialDefaultPeriod;
    }

    BuildStereoFloatFormat(output_format_, object_format_ ? object_format_->nSamplesPerSec : 48000);
    hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED, kSpatialStreamFlags, default_period, 0, &output_format_.Format, nullptr);
    Log::Info("IAudioClient::Initialize internal stream=%p result=0x%08lX mode=shared flags=0x%08lX buffer=%lld periodicity=0 format=%s",
              this,
              static_cast<unsigned long>(hr),
              static_cast<unsigned long>(kSpatialStreamFlags),
              static_cast<long long>(default_period),
              DescribeWaveFormat(&output_format_.Format).c_str());
    if (FAILED(hr)) {
        Log::Error("IAudioClient::Initialize internal failed stream=%p result=0x%08lX", this, static_cast<unsigned long>(hr));
        return hr;
    }

    period_frames_ = static_cast<UINT32>(MulDiv(default_period, output_format_.Format.nSamplesPerSec, 10000000));
    if (period_frames_ == 0) {
        period_frames_ = 1;
    }

    hr = audio_client_->GetBufferSize(&buffer_capacity_frames_);
    Log::Info("IAudioClient::GetBufferSize stream=%p result=0x%08lX capacity_frames=%u period_frames=%u",
              this,
              static_cast<unsigned long>(hr),
              buffer_capacity_frames_,
              period_frames_);
    if (FAILED(hr) || buffer_capacity_frames_ == 0) {
        Log::Error("IAudioClient::GetBufferSize initialization_error stream=%p result=0x%08lX capacity_frames=%u",
                   this, static_cast<unsigned long>(hr), buffer_capacity_frames_);
        return FAILED(hr) ? hr : E_FAIL;
    }

    UINT32 initial_padding = 0;
    const HRESULT initial_padding_hr = audio_client_->GetCurrentPadding(&initial_padding);
    Log::Info("Spatial buffer geometry stream=%p capacity_frames=%u period_frames=%u initial_padding_hr=0x%08lX initial_padding=%u format=%s",
              this,
              buffer_capacity_frames_,
              period_frames_,
              static_cast<unsigned long>(initial_padding_hr),
              initial_padding,
              DescribeWaveFormat(&output_format_.Format).c_str());
    if (FAILED(initial_padding_hr)) {
        Log::Error("IAudioClient::GetCurrentPadding initial failed stream=%p result=0x%08lX",
                   this, static_cast<unsigned long>(initial_padding_hr));
    }

    hr = audio_client_->SetEventHandle(event_handle_);
    Log::Info("IAudioClient::SetEventHandle stream=%p event=%p result=0x%08lX",
              this, event_handle_, static_cast<unsigned long>(hr));
    if (FAILED(hr)) {
        Log::Error("IAudioClient::SetEventHandle failed stream=%p result=0x%08lX", this, static_cast<unsigned long>(hr));
        return hr;
    }

    hr = audio_client_->GetService(IID_IAudioRenderClient, reinterpret_cast<void**>(&render_client_));
    Log::Info("IAudioClient::GetService IAudioRenderClient stream=%p result=0x%08lX service=%p",
              this, static_cast<unsigned long>(hr), render_client_);
    if (FAILED(hr) || !render_client_) {
        Log::Error("IAudioClient::GetService IAudioRenderClient failed stream=%p result=0x%08lX service=%p",
                   this, static_cast<unsigned long>(hr), render_client_);
        return FAILED(hr) ? hr : E_FAIL;
    }

    hr = audio_client_->GetService(IID_IAudioClock, reinterpret_cast<void**>(&audio_clock_));
    Log::Info("IAudioClient::GetService IAudioClock stream=%p result=0x%08lX service=%p",
              this, static_cast<unsigned long>(hr), audio_clock_);
    if (FAILED(hr) || !audio_clock_) {
        audio_clock_ = nullptr;
        Log::Warn("IAudioClock unavailable; watchdog cannot declare a playback stall stream=%p result=0x%08lX",
                  this, static_cast<unsigned long>(hr));
    } else {
        UINT64 frequency = 0;
        const HRESULT frequency_hr = audio_clock_->GetFrequency(&frequency);
        Log::Info("IAudioClock::GetFrequency stream=%p result=0x%08lX frequency=%llu",
                  this, static_cast<unsigned long>(frequency_hr), static_cast<unsigned long long>(frequency));
        if (FAILED(frequency_hr)) {
            Log::Error("IAudioClock::GetFrequency failed stream=%p result=0x%08lX", this,
                       static_cast<unsigned long>(frequency_hr));
        }
    }

    update_frames_ = ~0u;
    StartWatchdog();
    Log::Info("Spatial output activated stream=%p period_frames=%u capacity_frames=%u recovery_enabled=%d",
              this, period_frames_, buffer_capacity_frames_, g_config.recovery_enabled);
    return S_OK;
}

bool SpatialRenderStream::MixToStereo()
{
    if (!render_buffer_) {
        return false;
    }

    bool non_silent = false;
    std::fill(render_buffer_, render_buffer_ + static_cast<size_t>(update_frames_) * 2, 0.0f);
    for (SpatialRenderObject* object : objects_) {
        const StereoMixCoefficients gains = GetStereoMixCoefficients(object->Type());
        if (gains.left == 0.0f && gains.right == 0.0f) {
            continue;
        }

        const auto& samples = object->Samples();
        for (UINT32 index = 0; index < update_frames_; ++index) {
            non_silent = non_silent || std::fabs(samples[index]) > 0.000001f;
            render_buffer_[index * 2] += samples[index] * gains.left;
            render_buffer_[index * 2 + 1] += samples[index] * gains.right;
        }
    }
    return non_silent;
}

HRESULT STDMETHODCALLTYPE SpatialRenderStream::QueryInterface(REFIID riid, void** out)
{
    if (!out) {
        return E_POINTER;
    }

    *out = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ISpatialAudioObjectRenderStreamBase) ||
        IsEqualIID(riid, IID_ISpatialAudioObjectRenderStream)) {
        *out = static_cast<ISpatialAudioObjectRenderStream*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE SpatialRenderStream::AddRef()
{
    return ref_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE SpatialRenderStream::Release()
{
    const ULONG ref = ref_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (ref == 0) {
        delete this;
    }
    return ref;
}

HRESULT STDMETHODCALLTYPE SpatialRenderStream::GetAvailableDynamicObjectCount(UINT32* count)
{
    if (!count) {
        return E_POINTER;
    }
    *count = 0;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialRenderStream::GetService(REFIID, void** service)
{
    if (service) {
        *service = nullptr;
    }
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE SpatialRenderStream::Start()
{
    EnterCriticalSection(&lock_);
    if (recovery_in_progress_) {
        LeaveCriticalSection(&lock_);
        Log::Error("IAudioClient::Start reentrant_during_recovery stream=%p result=0x%08lX",
                   this, static_cast<unsigned long>(AUDCLNT_E_BUFFER_OPERATION_PENDING));
        return AUDCLNT_E_BUFFER_OPERATION_PENDING;
    }
    const HRESULT hr = audio_client_ ? audio_client_->Start() : E_FAIL;
    const bool previous_started = started_.load(std::memory_order_acquire);
    started_.store(StartedStateAfterStartResult(previous_started, static_cast<std::int32_t>(hr)),
                   std::memory_order_release);
    const bool started = started_.load(std::memory_order_acquire);
    LeaveCriticalSection(&lock_);
    Log::Info("IAudioClient::Start stream=%p result=0x%08lX started=%d",
              this, static_cast<unsigned long>(hr), started);
    if (FAILED(hr)) {
        Log::Error("IAudioClient::Start failed stream=%p result=0x%08lX", this, static_cast<unsigned long>(hr));
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE SpatialRenderStream::Stop()
{
    EnterCriticalSection(&lock_);
    if (recovery_in_progress_) {
        LeaveCriticalSection(&lock_);
        Log::Error("IAudioClient::Stop reentrant_during_recovery stream=%p result=0x%08lX",
                   this, static_cast<unsigned long>(AUDCLNT_E_BUFFER_OPERATION_PENDING));
        return AUDCLNT_E_BUFFER_OPERATION_PENDING;
    }
    const HRESULT hr = audio_client_ ? audio_client_->Stop() : E_FAIL;
    if (SUCCEEDED(hr)) {
        started_.store(false, std::memory_order_release);
    }
    const bool started = started_.load(std::memory_order_acquire);
    LeaveCriticalSection(&lock_);
    Log::Info("IAudioClient::Stop stream=%p result=0x%08lX started=%d",
              this, static_cast<unsigned long>(hr), started);
    if (FAILED(hr)) {
        Log::Error("IAudioClient::Stop failed stream=%p result=0x%08lX", this, static_cast<unsigned long>(hr));
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE SpatialRenderStream::Reset()
{
    EnterCriticalSection(&lock_);
    if (recovery_in_progress_) {
        LeaveCriticalSection(&lock_);
        Log::Error("IAudioClient::Reset reentrant_during_recovery stream=%p result=0x%08lX",
                   this, static_cast<unsigned long>(AUDCLNT_E_BUFFER_OPERATION_PENDING));
        return AUDCLNT_E_BUFFER_OPERATION_PENDING;
    }
    const HRESULT hr = audio_client_ ? audio_client_->Reset() : E_FAIL;
    if (SUCCEEDED(hr)) {
        started_.store(false, std::memory_order_release);
        last_non_silent_qpc_ = 0;
        last_callback_qpc_ = 0;
    }
    const bool started = started_.load(std::memory_order_acquire);
    LeaveCriticalSection(&lock_);
    Log::Info("IAudioClient::Reset stream=%p result=0x%08lX started=%d",
              this, static_cast<unsigned long>(hr), started);
    if (FAILED(hr)) {
        Log::Error("IAudioClient::Reset failed stream=%p result=0x%08lX", this, static_cast<unsigned long>(hr));
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE SpatialRenderStream::BeginUpdatingAudioObjects(UINT32* dynamic_count, UINT32* frames)
{
    if (!dynamic_count || !frames || !render_client_) {
        Log::Error("Audio event callback/BeginUpdatingAudioObjects invalid args stream=%p dynamic_count=%p frames=%p render_client=%p",
                   this, dynamic_count, frames, render_client_);
        return E_POINTER;
    }

    EnterCriticalSection(&lock_);
    if (update_frames_ != ~0u) {
        LeaveCriticalSection(&lock_);
        Log::Warn("Audio event callback/BeginUpdatingAudioObjects out of order stream=%p", this);
        return SPTLAUDCLNT_E_OUT_OF_ORDER;
    }
    if (recovery_in_progress_) {
        LeaveCriticalSection(&lock_);
        Log::Error("BeginUpdatingAudioObjects reentrant_during_recovery stream=%p result=0x%08lX",
                   this, static_cast<unsigned long>(AUDCLNT_E_BUFFER_OPERATION_PENDING));
        return AUDCLNT_E_BUFFER_OPERATION_PENDING;
    }

    AudioClientRecoveryBackend backend(audio_client_, render_client_, started_, this);
    const bool inject_buffer_too_large = recovery_.ShouldInject(
        successful_get_buffer_cycles_.load(std::memory_order_acquire));
    recovery_in_progress_ = true;
    BufferRecovery::AcquireOutcome outcome = recovery_.Acquire(
        backend, period_frames_, buffer_capacity_frames_, inject_buffer_too_large);
    recovery_in_progress_ = false;

    if (outcome.reset_succeeded) {
        last_non_silent_qpc_.store(0, std::memory_order_release);
        last_callback_qpc_.store(0, std::memory_order_release);
    }
    const std::uint64_t now = QpcNow();
    last_callback_qpc_.store(now, std::memory_order_release);
    const std::uint64_t operation_count =
        get_buffer_count_.fetch_add(outcome.get_buffer_calls, std::memory_order_relaxed) + outcome.get_buffer_calls;
    HRESULT hr = static_cast<HRESULT>(outcome.hr);
    const HRESULT previous_hr = static_cast<HRESULT>(last_get_buffer_hr_.exchange(hr, std::memory_order_acq_rel));
    if (FAILED(hr) || !outcome.buffer || outcome.frames == 0 || outcome.frames > period_frames_) {
        if (SUCCEEDED(hr)) {
            hr = E_FAIL;
            last_get_buffer_hr_.store(hr, std::memory_order_release);
        }
        get_buffer_failures_.fetch_add(1, std::memory_order_relaxed);
        update_frames_ = ~0u;
        render_buffer_ = nullptr;
        LeaveCriticalSection(&lock_);
        LogBufferRecoveryOutcome(outcome);
        if (hr != previous_hr) {
            Log::Error("IAudioRenderClient::GetBuffer failure_transition stream=%p count=%llu frames=%u physical_calls=%u previous=0x%08lX result=0x%08lX",
                       this,
                       static_cast<unsigned long long>(operation_count),
                       period_frames_,
                       outcome.get_buffer_calls,
                       static_cast<unsigned long>(previous_hr),
                       static_cast<unsigned long>(hr));
        }
        return hr;
    }

    update_frames_ = outcome.frames;
    render_buffer_ = static_cast<float*>(outcome.buffer);
    successful_get_buffer_cycles_.fetch_add(1, std::memory_order_relaxed);

    if (FAILED(previous_hr)) {
        Log::Warn("IAudioRenderClient::GetBuffer recovered stream=%p count=%llu previous=0x%08lX result=0x%08lX",
                  this,
                  static_cast<unsigned long long>(operation_count),
                  static_cast<unsigned long>(previous_hr),
                  static_cast<unsigned long>(hr));
    } else if (g_config.diagnostics_detailed_buffer_logging &&
               operation_count % static_cast<std::uint64_t>(g_config.diagnostics_buffer_log_sample_rate) == 0) {
        Log::Info("IAudioRenderClient::GetBuffer sampled stream=%p count=%llu frames=%u result=0x%08lX buffer=%p",
                  this,
                  static_cast<unsigned long long>(operation_count),
                  update_frames_,
                  static_cast<unsigned long>(hr),
                  render_buffer_);
    }

    for (SpatialRenderObject* object : objects_) {
        std::fill(object->Samples().begin(), object->Samples().begin() + update_frames_, 0.0f);
    }

    *dynamic_count = 0;
    *frames = update_frames_;
    LeaveCriticalSection(&lock_);
    LogBufferRecoveryOutcome(outcome);
    return S_OK;
}

void SpatialRenderStream::LogBufferRecoveryOutcome(const BufferRecovery::AcquireOutcome& outcome)
{
#if METAPHOR_COMPLETE_AUDIO_PATCH_ENABLE_FAULT_INJECTION
    if (outcome.fault_injected) {
        Log::Warn("FAULT_INJECTION_BUFFER_TOO_LARGE stream=%p successful_cycles=%llu threshold=%d",
                  this,
                  static_cast<unsigned long long>(successful_get_buffer_cycles_.load(std::memory_order_relaxed)),
                  g_config.recovery_fault_inject_buffer_too_large_after);
    }
#endif
    if (!outcome.buffer_too_large_caught) {
        return;
    }

    Log::RecoveryWarn("BUFFER_TOO_LARGE_CAUGHT stream=%p original_request=%u capacity=%u padding_hr=0x%08lX padding=%u available=%u retry=%u retry_hr=0x%08lX",
              this,
              outcome.original_request_frames,
              outcome.buffer_capacity_frames,
              static_cast<unsigned long>(outcome.padding_hr),
              outcome.padding_frames,
              outcome.available_frames,
              outcome.adaptive_retry_frames,
              static_cast<unsigned long>(outcome.adaptive_hr));

    if (outcome.adaptive_succeeded) {
        Log::RecoveryWarn("ADAPTIVE_BUFFER_RETRY_SUCCEEDED stream=%p original_request=%u capacity=%u padding=%u available=%u retry=%u result=0x%08lX",
                  this,
                  outcome.original_request_frames,
                  outcome.buffer_capacity_frames,
                  outcome.padding_frames,
                  outcome.available_frames,
                  outcome.adaptive_retry_frames,
                  static_cast<unsigned long>(outcome.adaptive_hr));
    } else if (g_config.recovery_adaptive_buffer_retry) {
        Log::RecoveryError("ADAPTIVE_BUFFER_RETRY_FAILED stream=%p attempted=%d original_request=%u capacity=%u padding_hr=0x%08lX padding=%u available=%u retry=%u result=0x%08lX",
                   this,
                   outcome.adaptive_attempted,
                   outcome.original_request_frames,
                   outcome.buffer_capacity_frames,
                   static_cast<unsigned long>(outcome.padding_hr),
                   outcome.padding_frames,
                   outcome.available_frames,
                   outcome.adaptive_retry_frames,
                   static_cast<unsigned long>(outcome.adaptive_hr));
    }

    if (outcome.circuit_breaker_open) {
        Log::RecoveryError("RECOVERY_CIRCUIT_BREAKER_OPEN stream=%p maximum_per_window=%d window_ms=%d cooldown_ms=%d",
                   this,
                   g_config.recovery_maximum_recoveries_per_window,
                   g_config.recovery_window_ms,
                   g_config.recovery_cooldown_ms);
        return;
    }
    if (!outcome.recovery_started) {
        return;
    }

    const double elapsed_ms = g_qpc_frequency.QuadPart > 0
        ? static_cast<double>(outcome.recovery_elapsed_ticks) * 1000.0 /
              static_cast<double>(g_qpc_frequency.QuadPart)
        : 0.0;
    if (outcome.recovery_succeeded) {
        Log::Warn("BUFFER_RECOVERY_SUCCEEDED stream=%p stop_hr=0x%08lX reset_hr=0x%08lX start_hr=0x%08lX started_after_start=%d padding_hr=0x%08lX padding=%u available=%u retry=%u retry_hr=0x%08lX elapsed_ms=%.3f",
                  this,
                  static_cast<unsigned long>(outcome.stop_hr),
                  static_cast<unsigned long>(outcome.reset_hr),
                  static_cast<unsigned long>(outcome.start_hr),
                  outcome.started_after_start,
                  static_cast<unsigned long>(outcome.recovery_padding_hr),
                  outcome.recovery_padding_frames,
                  outcome.recovery_available_frames,
                  outcome.recovery_retry_frames,
                  static_cast<unsigned long>(outcome.recovery_retry_hr),
                  elapsed_ms);
    } else if (outcome.recovery_failed) {
        Log::Error("BUFFER_RECOVERY_FAILED stream=%p stop_hr=0x%08lX reset_hr=0x%08lX start_hr=0x%08lX started_after_reset=%d started_after_start=%d padding_hr=0x%08lX padding=%u available=%u retry=%u retry_hr=0x%08lX elapsed_ms=%.3f",
                   this,
                   static_cast<unsigned long>(outcome.stop_hr),
                   static_cast<unsigned long>(outcome.reset_hr),
                   static_cast<unsigned long>(outcome.start_hr),
                   outcome.started_after_reset,
                   outcome.started_after_start,
                   static_cast<unsigned long>(outcome.recovery_padding_hr),
                   outcome.recovery_padding_frames,
                   outcome.recovery_available_frames,
                   outcome.recovery_retry_frames,
                   static_cast<unsigned long>(outcome.recovery_retry_hr),
                   elapsed_ms);
    }
}

HRESULT STDMETHODCALLTYPE SpatialRenderStream::EndUpdatingAudioObjects()
{
    if (!render_client_) {
        Log::Error("IAudioRenderClient::ReleaseBuffer unavailable stream=%p", this);
        return E_FAIL;
    }

    EnterCriticalSection(&lock_);
    if (update_frames_ == ~0u) {
        LeaveCriticalSection(&lock_);
        Log::Warn("IAudioRenderClient::ReleaseBuffer out of order stream=%p", this);
        return SPTLAUDCLNT_E_OUT_OF_ORDER;
    }

    const bool non_silent = MixToStereo();
    const UINT32 frames = update_frames_;
    const HRESULT hr = render_client_->ReleaseBuffer(frames, 0);
    update_frames_ = ~0u;
    render_buffer_ = nullptr;
    LeaveCriticalSection(&lock_);
    const std::uint64_t now = QpcNow();
    last_callback_qpc_.store(now, std::memory_order_release);
    if (non_silent) {
        last_non_silent_qpc_.store(now, std::memory_order_release);
    }
    const std::uint64_t operation_count = release_buffer_count_.fetch_add(1, std::memory_order_relaxed) + 1;
    const HRESULT previous_hr = static_cast<HRESULT>(last_release_buffer_hr_.exchange(hr, std::memory_order_acq_rel));
    if (FAILED(hr)) {
        release_buffer_failures_.fetch_add(1, std::memory_order_relaxed);
        if (hr != previous_hr) {
            Log::Error("IAudioRenderClient::ReleaseBuffer failure_transition stream=%p count=%llu frames=%u previous=0x%08lX result=0x%08lX samples=%s",
                       this,
                       static_cast<unsigned long long>(operation_count),
                       frames,
                       static_cast<unsigned long>(previous_hr),
                       static_cast<unsigned long>(hr),
                       non_silent ? "non-silent" : "silent");
        }
    } else {
        total_submitted_frames_.fetch_add(frames, std::memory_order_relaxed);
        if (FAILED(previous_hr)) {
            Log::Warn("IAudioRenderClient::ReleaseBuffer recovered stream=%p count=%llu previous=0x%08lX result=0x%08lX",
                      this,
                      static_cast<unsigned long long>(operation_count),
                      static_cast<unsigned long>(previous_hr),
                      static_cast<unsigned long>(hr));
        } else if (g_config.diagnostics_detailed_buffer_logging &&
                   operation_count % static_cast<std::uint64_t>(g_config.diagnostics_buffer_log_sample_rate) == 0) {
            Log::Info("IAudioRenderClient::ReleaseBuffer sampled stream=%p count=%llu frames=%u flags=0 samples=%s result=0x%08lX",
                      this,
                      static_cast<unsigned long long>(operation_count),
                      frames,
                      non_silent ? "non-silent" : "silent",
                      static_cast<unsigned long>(hr));
        }
    }
    return hr;
}

void SpatialRenderStream::StartWatchdog()
{
    if (!g_config.diagnostics_enabled || watchdog_thread_) {
        return;
    }
    watchdog_stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!watchdog_stop_event_) {
        Log::Error("Watchdog CreateEvent failed stream=%p win32_error=%lu", this, GetLastError());
        return;
    }
    watchdog_thread_ = CreateThread(nullptr, 0, &SpatialRenderStream::WatchdogThread, this, 0, nullptr);
    if (!watchdog_thread_) {
        Log::Error("Watchdog CreateThread failed stream=%p win32_error=%lu", this, GetLastError());
        CloseHandle(watchdog_stop_event_);
        watchdog_stop_event_ = nullptr;
        return;
    }
    Log::Info("Observation-only watchdog started stream=%p poll_ms=%d stall_ms=%d",
              this, g_config.watchdog_poll_interval_ms, g_config.watchdog_stall_timeout_ms);
}

void SpatialRenderStream::StopWatchdog()
{
    if (!watchdog_thread_) {
        return;
    }
    SetEvent(watchdog_stop_event_);
    WaitForSingleObject(watchdog_thread_, INFINITE);
    CloseHandle(watchdog_thread_);
    CloseHandle(watchdog_stop_event_);
    watchdog_thread_ = nullptr;
    watchdog_stop_event_ = nullptr;
    Log::Info("Observation-only watchdog stopped stream=%p", this);
}

DWORD WINAPI SpatialRenderStream::WatchdogThread(void* context)
{
    auto* stream = static_cast<SpatialRenderStream*>(context);
    const HRESULT coinit_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    stream->WatchdogLoop();
    if (SUCCEEDED(coinit_hr)) {
        CoUninitialize();
    }
    return 0;
}

void SpatialRenderStream::WatchdogLoop()
{
    StallDetector detector({
        MillisecondsToQpc(g_config.watchdog_stall_timeout_ms),
        MillisecondsToQpc(g_config.watchdog_non_silent_window_ms),
        MillisecondsToQpc(g_config.watchdog_buffer_activity_window_ms),
    });
    const std::uint64_t status_interval = MillisecondsToQpc(g_config.diagnostics_periodic_status_ms);
    const std::uint64_t error_reminder_interval = MillisecondsToQpc(g_config.diagnostics_error_reminder_ms);
    std::uint64_t last_status = 0;
    struct HrLogState {
        bool initialized = false;
        HRESULT last_hr = S_OK;
        std::uint64_t last_error_log = 0;
    } padding_state, position_state, frequency_state;

    auto log_hr_state = [&](const char* operation, HRESULT hr, HrLogState& state, std::uint64_t now) {
        if (!state.initialized || hr != state.last_hr) {
            if (FAILED(hr)) {
                Log::Error("%s failure_transition stream=%p previous=0x%08lX result=0x%08lX",
                           operation,
                           this,
                           static_cast<unsigned long>(state.last_hr),
                           static_cast<unsigned long>(hr));
                state.last_error_log = now;
            } else if (state.initialized && FAILED(state.last_hr)) {
                Log::Warn("%s recovered stream=%p previous=0x%08lX result=0x%08lX",
                          operation,
                          this,
                          static_cast<unsigned long>(state.last_hr),
                          static_cast<unsigned long>(hr));
            }
            state.initialized = true;
            state.last_hr = hr;
        } else if (FAILED(hr) && now - state.last_error_log >= error_reminder_interval) {
            Log::Error("%s failure_reminder stream=%p result=0x%08lX reminder_ms=%d",
                       operation,
                       this,
                       static_cast<unsigned long>(hr),
                       g_config.diagnostics_error_reminder_ms);
            state.last_error_log = now;
        }
    };

    while (WaitForSingleObject(watchdog_stop_event_, static_cast<DWORD>(g_config.watchdog_poll_interval_ms)) == WAIT_TIMEOUT) {
        const std::uint64_t now = QpcNow();
        const bool periodic = last_status == 0 || now - last_status >= status_interval;
        UINT32 padding = 0;
        UINT64 position = 0;
        UINT64 qpc_position_100ns = 0;
        UINT64 frequency = 0;
        HRESULT padding_hr = E_PENDING;
        HRESULT position_hr = E_PENDING;
        HRESULT frequency_hr = E_PENDING;
        if (!TryEnterCriticalSection(&lock_)) {
            continue;
        }
        padding_hr = audio_client_ ? audio_client_->GetCurrentPadding(&padding) : E_FAIL;
        position_hr = audio_clock_ ? audio_clock_->GetPosition(&position, &qpc_position_100ns) : E_NOINTERFACE;
        if (periodic) {
            frequency_hr = audio_clock_ ? audio_clock_->GetFrequency(&frequency) : E_NOINTERFACE;
        }
        LeaveCriticalSection(&lock_);

        log_hr_state("IAudioClient::GetCurrentPadding", padding_hr, padding_state, now);
        log_hr_state("IAudioClock::GetPosition", position_hr, position_state, now);

        if (periodic) {
            log_hr_state("IAudioClock::GetFrequency", frequency_hr, frequency_state, now);
            Log::Info("Watchdog status stream=%p started=%d padding_hr=0x%08lX padding=%u clock_hr=0x%08lX position=%llu qpc_position_100ns=%llu frequency_hr=0x%08lX frequency=%llu get_buffers=%llu release_buffers=%llu total_frames=%llu get_failures=%llu release_failures=%llu last_get_hr=0x%08lX last_release_hr=0x%08lX last_callback_qpc=%llu last_non_silent_qpc=%llu",
                      this,
                      started_.load(std::memory_order_acquire),
                      static_cast<unsigned long>(padding_hr),
                      padding,
                      static_cast<unsigned long>(position_hr),
                      static_cast<unsigned long long>(position),
                      static_cast<unsigned long long>(qpc_position_100ns),
                      static_cast<unsigned long>(frequency_hr),
                      static_cast<unsigned long long>(frequency),
                      static_cast<unsigned long long>(get_buffer_count_.load(std::memory_order_relaxed)),
                      static_cast<unsigned long long>(release_buffer_count_.load(std::memory_order_relaxed)),
                      static_cast<unsigned long long>(total_submitted_frames_.load(std::memory_order_relaxed)),
                      static_cast<unsigned long long>(get_buffer_failures_.load(std::memory_order_relaxed)),
                      static_cast<unsigned long long>(release_buffer_failures_.load(std::memory_order_relaxed)),
                      static_cast<unsigned long>(last_get_buffer_hr_.load(std::memory_order_relaxed)),
                      static_cast<unsigned long>(last_release_buffer_hr_.load(std::memory_order_relaxed)),
                      static_cast<unsigned long long>(last_callback_qpc_.load(std::memory_order_relaxed)),
                      static_cast<unsigned long long>(last_non_silent_qpc_.load(std::memory_order_relaxed)));
            last_status = now;
        }

        StallObservation observation{};
        observation.now_ticks = now;
        observation.started = started_.load(std::memory_order_acquire);
        observation.playback_measurement_valid = SUCCEEDED(position_hr);
        observation.playback_position = position;
        observation.last_non_silent_ticks = last_non_silent_qpc_.load(std::memory_order_acquire);
        observation.last_buffer_operation_ticks = last_callback_qpc_.load(std::memory_order_acquire);
        observation.callback_count = release_buffer_count_.load(std::memory_order_acquire);
        observation.buffer_operations_expected = observation.started && event_handle_ != nullptr;

        const StallTransition transition = detector.Observe(observation);
        if (transition == StallTransition::SuspectedClockStallWithSubmissions) {
            Log::Error("SUSPECTED_CLOCK_STALL_WITH_SUBMISSIONS observation_only=1 stream=%p timeout_ms=%d position=%llu padding=%u callbacks=%llu last_non_silent_qpc=%llu last_callback_qpc=%llu",
                       this,
                       g_config.watchdog_stall_timeout_ms,
                       static_cast<unsigned long long>(position),
                       padding,
                       static_cast<unsigned long long>(observation.callback_count),
                       static_cast<unsigned long long>(observation.last_non_silent_ticks),
                       static_cast<unsigned long long>(observation.last_buffer_operation_ticks));
        } else if (transition == StallTransition::SuspectedRenderCallbackStarvation) {
            Log::Error("SUSPECTED_RENDER_CALLBACK_STARVATION observation_only=1 stream=%p timeout_ms=%d position=%llu padding=%u callbacks=%llu last_non_silent_qpc=%llu last_callback_qpc=%llu",
                       this,
                       g_config.watchdog_stall_timeout_ms,
                       static_cast<unsigned long long>(position),
                       padding,
                       static_cast<unsigned long long>(observation.callback_count),
                       static_cast<unsigned long long>(observation.last_non_silent_ticks),
                       static_cast<unsigned long long>(observation.last_buffer_operation_ticks));
        } else if (transition == StallTransition::Cleared) {
            Log::Warn("SUSPECTED_AUDIO_STALL_CONDITION_CLEARED stream=%p position=%llu",
                      this, static_cast<unsigned long long>(position));
        }
    }
}

HRESULT STDMETHODCALLTYPE SpatialRenderStream::ActivateSpatialAudioObject(AudioObjectType type, ISpatialAudioObject** out)
{
    if (!out) {
        return E_POINTER;
    }

    *out = nullptr;
    if (type == AudioObjectType_Dynamic) {
        return SPTLAUDCLNT_E_NO_MORE_OBJECTS;
    }

    const UINT32 type_bits = static_cast<UINT32>(type);
    if (type_bits == 0 || (type_bits & (type_bits - 1)) != 0) {
        return E_INVALIDARG;
    }
    if ((static_cast<UINT32>(static_mask_) & type_bits) == 0) {
        return SPTLAUDCLNT_E_STATIC_OBJECT_NOT_AVAILABLE;
    }

    EnterCriticalSection(&lock_);
    for (SpatialRenderObject* object : objects_) {
        if (object->Type() == type) {
            LeaveCriticalSection(&lock_);
            return SPTLAUDCLNT_E_OBJECT_ALREADY_ACTIVE;
        }
    }

    auto* object = new (std::nothrow) SpatialRenderObject(this, type, period_frames_);
    if (!object) {
        LeaveCriticalSection(&lock_);
        return E_OUTOFMEMORY;
    }

    objects_.push_back(object);
    LeaveCriticalSection(&lock_);
    *out = object;
    return S_OK;
}

SpatialAudioWrapper::SpatialAudioWrapper(IMMDevice* device, ISpatialAudioClient* passthrough)
    : device_(device), passthrough_(passthrough)
{
    if (device_) {
        device_->AddRef();
    }
    if (passthrough_) {
        passthrough_->AddRef();
    }

    std::memset(&object_format_, 0, sizeof(object_format_));
    object_format_.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    object_format_.nChannels = 1;
    object_format_.nSamplesPerSec = 48000;
    object_format_.wBitsPerSample = 32;
    object_format_.nBlockAlign = 4;
    object_format_.nAvgBytesPerSec = object_format_.nSamplesPerSec * object_format_.nBlockAlign;
    object_format_.cbSize = 0;
}

SpatialAudioWrapper::~SpatialAudioWrapper()
{
    if (passthrough_) {
        passthrough_->Release();
    }
    if (device_) {
        device_->Release();
    }
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::QueryInterface(REFIID riid, void** out)
{
    if (!out) {
        return E_POINTER;
    }

    *out = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ISpatialAudioClient)) {
        *out = static_cast<ISpatialAudioClient*>(this);
    } else if (IsEqualIID(riid, IID_IAudioFormatEnumerator)) {
        *out = static_cast<IAudioFormatEnumerator*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE SpatialAudioWrapper::AddRef()
{
    return ref_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE SpatialAudioWrapper::Release()
{
    const ULONG ref = ref_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (ref == 0) {
        delete this;
    }
    return ref;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::GetStaticObjectPosition(AudioObjectType, float* x, float* y, float* z)
{
    if (x) {
        *x = 0.0f;
    }
    if (y) {
        *y = 0.0f;
    }
    if (z) {
        *z = 0.0f;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::GetNativeStaticObjectTypeMask(AudioObjectType* mask)
{
    if (!mask) {
        return E_POINTER;
    }
    *mask = static_cast<AudioObjectType>(kSpatialSupportedStaticMaskBits);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::GetMaxDynamicObjectCount(UINT32* value)
{
    if (!value) {
        return E_POINTER;
    }
    *value = 0;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::GetSupportedAudioObjectFormatEnumerator(IAudioFormatEnumerator** enumerator)
{
    if (!enumerator) {
        return E_POINTER;
    }
    *enumerator = static_cast<IAudioFormatEnumerator*>(this);
    AddRef();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::GetMaxFrameCount(const WAVEFORMATEX* format, UINT32* count)
{
    if (!format || !count) {
        return E_POINTER;
    }
    *count = static_cast<UINT32>(MulDiv(kSpatialDefaultPeriod, format->nSamplesPerSec, 10000000));
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::IsAudioObjectFormatSupported(const WAVEFORMATEX* format)
{
    return WaveFormatsEqual(&object_format_, format) ? S_OK : AUDCLNT_E_UNSUPPORTED_FORMAT;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::IsSpatialAudioStreamAvailable(REFIID, const PROPVARIANT*)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::ActivateSpatialAudioStream(const PROPVARIANT* prop, REFIID riid, void** out)
{
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;

    if (g_teardown_requested.load(std::memory_order_acquire)) {
        const HRESULT hr = passthrough_ ? passthrough_->ActivateSpatialAudioStream(prop, riid, out) : E_ABORT;
        Log::Warn("ISpatialAudioClient::ActivateSpatialAudioStream teardown_passthrough result=0x%08lX stream=%p",
                  static_cast<unsigned long>(hr), out ? *out : nullptr);
        return hr;
    }

    if (!IsEqualIID(riid, IID_ISpatialAudioObjectRenderStream)) {
        const HRESULT hr = passthrough_ ? passthrough_->ActivateSpatialAudioStream(prop, riid, out) : E_NOINTERFACE;
        Log::Info("ISpatialAudioClient::ActivateSpatialAudioStream passthrough iid=%s result=0x%08lX stream=%p",
                  GuidToString(riid).c_str(), static_cast<unsigned long>(hr), out ? *out : nullptr);
        return hr;
    }

    if (!prop || prop->vt != VT_BLOB || prop->blob.cbSize != sizeof(SpatialAudioObjectRenderStreamActivationParams)) {
        return E_INVALIDARG;
    }

    const auto* params = reinterpret_cast<const SpatialAudioObjectRenderStreamActivationParams*>(prop->blob.pBlobData);
    if (!params || !params->ObjectFormat || !IsFloatObjectFormat(params->ObjectFormat) ||
        params->EventHandle == nullptr || params->EventHandle == INVALID_HANDLE_VALUE) {
        return E_INVALIDARG;
    }

    if ((static_cast<UINT32>(params->StaticObjectTypeMask) & static_cast<UINT32>(AudioObjectType_Dynamic)) != 0) {
        return E_INVALIDARG;
    }

    auto* stream = new (std::nothrow) SpatialRenderStream(this, *params);
    if (!stream) {
        return E_OUTOFMEMORY;
    }

    Log::Info("Spatial wrapper ActivateSpatialAudioStream static_mask=0x%08lX object_format=%s",
              static_cast<unsigned long>(params->StaticObjectTypeMask),
              DescribeWaveFormat(params->ObjectFormat).c_str());

    HRESULT hr = stream->ActivateOutput();
    if (FAILED(hr)) {
        Log::Warn("Spatial wrapper activation failed (0x%08lX), falling back to passthrough client", static_cast<unsigned long>(hr));
        delete stream;
        return passthrough_ ? passthrough_->ActivateSpatialAudioStream(prop, riid, out) : hr;
    }

    *out = static_cast<ISpatialAudioObjectRenderStream*>(stream);
    Log::Info("ISpatialAudioClient::ActivateSpatialAudioStream wrapped result=0x%08lX stream=%p", S_OK, *out);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::GetCount(UINT32* count)
{
    if (!count) {
        return E_POINTER;
    }
    *count = 1;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::GetFormat(UINT32 index, WAVEFORMATEX** format)
{
    if (!format) {
        return E_POINTER;
    }
    if (index != 0) {
        return E_INVALIDARG;
    }
    *format = &object_format_;
    return S_OK;
}

SpatialAudioWrapper* CreateSpatialAudioWrapper(IMMDevice* device, ISpatialAudioClient* passthrough)
{
    return new (std::nothrow) SpatialAudioWrapper(device, passthrough);
}

HRESULT STDMETHODCALLTYPE HookedAudioClientGetMixFormat(void* self, WAVEFORMATEX** device_format)
{
    g_saw_wasapi_activity = true;
    const HRESULT result = g_audio_client_get_mix_format(self, device_format);

    const WAVEFORMATEX* format = (device_format && *device_format) ? *device_format : nullptr;
    Log::Info("IAudioClient::GetMixFormat result=0x%08lX format=%s",
              static_cast<unsigned long>(result),
              DescribeWaveFormat(format).c_str());

    if (SUCCEEDED(result) && device_format && *device_format && g_config.force_stereo_mix_format &&
        ShouldForceStereoWaveFormat(*device_format)) {
        const std::string before = DescribeWaveFormat(*device_format);
        ForceStereoWaveFormatInPlace(*device_format);
        Log::Warn("Forced stereo GetMixFormat: %s -> %s", before.c_str(), DescribeWaveFormat(*device_format).c_str());
    }

    return result;
}

HRESULT STDMETHODCALLTYPE HookedAudioClientIsFormatSupported(
    void* self,
    AUDCLNT_SHAREMODE share_mode,
    const WAVEFORMATEX* format,
    WAVEFORMATEX** closest_match)
{
    g_saw_wasapi_activity = true;

    if (format && g_config.reject_multichannel_is_format_supported && format->nChannels > 2) {
        Log::Warn("Rejecting multichannel IsFormatSupported input share_mode=%s format=%s",
                  ShareModeToString(share_mode),
                  DescribeWaveFormat(format).c_str());
        if (closest_match) {
            *closest_match = nullptr;
        }
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }

    std::vector<std::uint8_t> format_copy_storage;
    const WAVEFORMATEX* format_to_use = format;
    if (format && g_config.force_stereo_is_format_supported && ShouldForceStereoWaveFormat(format)) {
        format_copy_storage = CloneWaveFormat(format);
        auto* format_copy = reinterpret_cast<WAVEFORMATEX*>(format_copy_storage.data());
        const std::string before = DescribeWaveFormat(format_copy);
        ForceStereoWaveFormatInPlace(format_copy);
        format_to_use = format_copy;
        Log::Warn("Forced stereo IsFormatSupported input: %s -> %s", before.c_str(), DescribeWaveFormat(format_copy).c_str());
    } else {
        Log::Info("IAudioClient::IsFormatSupported input share_mode=%s format=%s",
                  ShareModeToString(share_mode),
                  DescribeWaveFormat(format).c_str());
    }

    const HRESULT result = g_audio_client_is_format_supported(self, share_mode, format_to_use, closest_match);
    const WAVEFORMATEX* closest = (closest_match && *closest_match) ? *closest_match : nullptr;

    Log::Info("IAudioClient::IsFormatSupported result=0x%08lX closest_match=%s",
              static_cast<unsigned long>(result),
              DescribeWaveFormat(closest).c_str());
    return result;
}

HRESULT STDMETHODCALLTYPE HookedAudioClientInitialize(
    void* self,
    AUDCLNT_SHAREMODE share_mode,
    DWORD stream_flags,
    REFERENCE_TIME hns_buffer_duration,
    REFERENCE_TIME hns_periodicity,
    const WAVEFORMATEX* format,
    const GUID* audio_session_guid)
{
    g_saw_wasapi_activity = true;

    if (format && g_config.reject_multichannel_initialize && format->nChannels > 2) {
        Log::Warn("Rejecting multichannel Initialize input share_mode=%s flags=0x%08lX format=%s",
                  ShareModeToString(share_mode),
                  static_cast<unsigned long>(stream_flags),
                  DescribeWaveFormat(format).c_str());
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }

    std::vector<std::uint8_t> format_copy_storage;
    const WAVEFORMATEX* format_to_use = format;
    if (format && g_config.force_stereo_initialize && ShouldForceStereoWaveFormat(format)) {
        format_copy_storage = CloneWaveFormat(format);
        auto* format_copy = reinterpret_cast<WAVEFORMATEX*>(format_copy_storage.data());
        const std::string before = DescribeWaveFormat(format_copy);
        ForceStereoWaveFormatInPlace(format_copy);
        format_to_use = format_copy;
        Log::Warn("Forced stereo Initialize format: %s -> %s", before.c_str(), DescribeWaveFormat(format_copy).c_str());
    } else {
        Log::Info("IAudioClient::Initialize input share_mode=%s flags=0x%08lX buffer=%lld periodicity=%lld format=%s",
                  ShareModeToString(share_mode),
                  static_cast<unsigned long>(stream_flags),
                  static_cast<long long>(hns_buffer_duration),
                  static_cast<long long>(hns_periodicity),
                  DescribeWaveFormat(format).c_str());
    }

    const std::string session_guid = audio_session_guid ? GuidToString(*audio_session_guid) : "<null>";
    const HRESULT result = g_audio_client_initialize(
        self,
        share_mode,
        stream_flags,
        hns_buffer_duration,
        hns_periodicity,
        format_to_use,
        audio_session_guid
    );

    Log::Info("IAudioClient::Initialize result=0x%08lX session_guid=%s format_used=%s",
              static_cast<unsigned long>(result),
              session_guid.c_str(),
              DescribeWaveFormat(format_to_use).c_str());
    return result;
}

void HookAudioClient(void* audio_client)
{
    if (!audio_client) {
        return;
    }

    auto** vtable = *reinterpret_cast<void***>(audio_client);
    if (!vtable) {
        Log::Error("Failed to inspect IAudioClient vtable");
        return;
    }

    if (!g_hooked_audio_client_initialize.load()) {
        void* target = vtable[kIAudioClientInitializeIndex];
        if (target) {
            const MH_STATUS create_status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedAudioClientInitialize),
                                                          reinterpret_cast<void**>(&g_audio_client_initialize));
            if (create_status == MH_OK) {
                const MH_STATUS enable_status = MH_EnableHook(target);
                if (enable_status == MH_OK || enable_status == MH_ERROR_ENABLED) {
                    g_hooked_audio_client_initialize = true;
                    Log::Info("Hooked IAudioClient::Initialize");
                }
            } else {
                Log::Error("MH_CreateHook failed for IAudioClient::Initialize: %d", static_cast<int>(create_status));
            }
        }
    }

    if (!g_hooked_audio_client_is_format_supported.load()) {
        void* target = vtable[kIAudioClientIsFormatSupportedIndex];
        if (target) {
            const MH_STATUS create_status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedAudioClientIsFormatSupported),
                                                          reinterpret_cast<void**>(&g_audio_client_is_format_supported));
            if (create_status == MH_OK) {
                const MH_STATUS enable_status = MH_EnableHook(target);
                if (enable_status == MH_OK || enable_status == MH_ERROR_ENABLED) {
                    g_hooked_audio_client_is_format_supported = true;
                    Log::Info("Hooked IAudioClient::IsFormatSupported");
                }
            } else {
                Log::Error("MH_CreateHook failed for IAudioClient::IsFormatSupported: %d", static_cast<int>(create_status));
            }
        }
    }

    if (!g_hooked_audio_client_get_mix_format.load()) {
        void* target = vtable[kIAudioClientGetMixFormatIndex];
        if (target) {
            const MH_STATUS create_status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedAudioClientGetMixFormat),
                                                          reinterpret_cast<void**>(&g_audio_client_get_mix_format));
            if (create_status == MH_OK) {
                const MH_STATUS enable_status = MH_EnableHook(target);
                if (enable_status == MH_OK || enable_status == MH_ERROR_ENABLED) {
                    g_hooked_audio_client_get_mix_format = true;
                    Log::Info("Hooked IAudioClient::GetMixFormat");
                }
            } else {
                Log::Error("MH_CreateHook failed for IAudioClient::GetMixFormat: %d", static_cast<int>(create_status));
            }
        }
    }
}

HRESULT STDMETHODCALLTYPE HookedIMMDeviceActivate(void* self, REFIID iid, DWORD clsctx, PROPVARIANT* activation_params, void** out)
{
    g_saw_wasapi_activity = true;
    if (g_config.disable_spatial_audio_client && IsEqualIID(iid, kIidISpatialAudioClient)) {
        Log::Warn("Blocking IMMDevice::Activate for ISpatialAudioClient and returning E_NOINTERFACE");
        if (out) {
            *out = nullptr;
        }
        return E_NOINTERFACE;
    }

    if (g_config.spatial_wrapper_enabled && IsEqualIID(iid, kIidISpatialAudioClient)) {
        void* passthrough = nullptr;
        const HRESULT result = g_immdevice_activate(self, iid, clsctx, activation_params, &passthrough);

        Log::Info("IMMDevice::Activate iid=%s clsctx=0x%08lX result=0x%08lX instance=%p",
                  GuidToString(iid).c_str(),
                  static_cast<unsigned long>(clsctx),
                  static_cast<unsigned long>(result),
                  passthrough);

        if (FAILED(result) || !passthrough) {
            if (out) {
                *out = passthrough;
            }
            return result;
        }

        SpatialAudioWrapper* wrapper = CreateSpatialAudioWrapper(
            reinterpret_cast<IMMDevice*>(self),
            static_cast<ISpatialAudioClient*>(passthrough)
        );
        if (!wrapper) {
            Log::Error("Failed to allocate spatial wrapper, returning passthrough client");
            if (out) {
                *out = passthrough;
            }
            return result;
        }

        static_cast<ISpatialAudioClient*>(passthrough)->Release();
        if (out) {
            *out = static_cast<ISpatialAudioClient*>(wrapper);
        }
        Log::Info("Returning wrapped ISpatialAudioClient passthrough=%p wrapper=%p",
                  passthrough,
                  static_cast<ISpatialAudioClient*>(wrapper));
        return S_OK;
    }

    const HRESULT result = g_immdevice_activate(self, iid, clsctx, activation_params, out);

    Log::Info("IMMDevice::Activate iid=%s clsctx=0x%08lX result=0x%08lX instance=%p",
              GuidToString(iid).c_str(),
              static_cast<unsigned long>(clsctx),
              static_cast<unsigned long>(result),
              out ? *out : nullptr);

    if (SUCCEEDED(result) && out && *out && IsAudioClientIid(iid)) {
        HookAudioClient(*out);
    }

    return result;
}

void HookIMMDevice(void* device)
{
    if (g_hooked_immdevice_activate.load() || !device) {
        return;
    }

    auto** vtable = *reinterpret_cast<void***>(device);
    if (!vtable) {
        Log::Error("Failed to inspect IMMDevice vtable");
        return;
    }

    void* target = vtable[kIMMDeviceActivateIndex];
    if (!target) {
        Log::Error("IMMDevice::Activate vtable slot was null");
        return;
    }

    const MH_STATUS create_status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedIMMDeviceActivate),
                                                  reinterpret_cast<void**>(&g_immdevice_activate));
    if (create_status != MH_OK) {
        Log::Error("MH_CreateHook failed for IMMDevice::Activate: %d", static_cast<int>(create_status));
        return;
    }

    const MH_STATUS enable_status = MH_EnableHook(target);
    if (enable_status != MH_OK && enable_status != MH_ERROR_ENABLED) {
        Log::Error("MH_EnableHook failed for IMMDevice::Activate: %d", static_cast<int>(enable_status));
        return;
    }

    g_hooked_immdevice_activate = true;
    Log::Info("Hooked IMMDevice::Activate");
}

HRESULT STDMETHODCALLTYPE HookedGetDefaultAudioEndpoint(void* self, EDataFlow flow, ERole role, IMMDevice** device)
{
    g_saw_wasapi_activity = true;
    const HRESULT result = g_get_default_audio_endpoint(self, flow, role, device);

    Log::Info("IMMDeviceEnumerator::GetDefaultAudioEndpoint flow=%d role=%d result=0x%08lX device=%p",
              static_cast<int>(flow),
              static_cast<int>(role),
              static_cast<unsigned long>(result),
              device ? *device : nullptr);

    if (SUCCEEDED(result) && device && *device) {
        HookIMMDevice(*device);
    }

    return result;
}

HRESULT STDMETHODCALLTYPE HookedGetDevice(void* self, LPCWSTR device_id, IMMDevice** device)
{
    g_saw_wasapi_activity = true;
    const HRESULT result = g_get_device(self, device_id, device);

    Log::Info("IMMDeviceEnumerator::GetDevice id=%s result=0x%08lX device=%p",
              device_id ? Narrow(std::wstring_view(device_id)).c_str() : "<null>",
              static_cast<unsigned long>(result),
              device ? *device : nullptr);

    if (SUCCEEDED(result) && device && *device) {
        HookIMMDevice(*device);
    }

    return result;
}

void HookIMMDeviceEnumerator(void* enumerator)
{
    if (!enumerator) {
        return;
    }

    auto** vtable = *reinterpret_cast<void***>(enumerator);
    if (!vtable) {
        Log::Error("Failed to inspect IMMDeviceEnumerator vtable");
        return;
    }

    if (!g_hooked_get_default_audio_endpoint.load()) {
        void* target = vtable[kIMMDeviceEnumeratorGetDefaultAudioEndpointIndex];
        if (target) {
            const MH_STATUS create_status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedGetDefaultAudioEndpoint),
                                                          reinterpret_cast<void**>(&g_get_default_audio_endpoint));
            if (create_status == MH_OK) {
                const MH_STATUS enable_status = MH_EnableHook(target);
                if (enable_status == MH_OK || enable_status == MH_ERROR_ENABLED) {
                    g_hooked_get_default_audio_endpoint = true;
                    Log::Info("Hooked IMMDeviceEnumerator::GetDefaultAudioEndpoint");
                }
            } else {
                Log::Error("MH_CreateHook failed for GetDefaultAudioEndpoint: %d", static_cast<int>(create_status));
            }
        }
    }

    if (!g_hooked_get_device.load()) {
        void* target = vtable[kIMMDeviceEnumeratorGetDeviceIndex];
        if (target) {
            const MH_STATUS create_status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedGetDevice),
                                                          reinterpret_cast<void**>(&g_get_device));
            if (create_status == MH_OK) {
                const MH_STATUS enable_status = MH_EnableHook(target);
                if (enable_status == MH_OK || enable_status == MH_ERROR_ENABLED) {
                    g_hooked_get_device = true;
                    Log::Info("Hooked IMMDeviceEnumerator::GetDevice");
                }
            } else {
                Log::Error("MH_CreateHook failed for GetDevice: %d", static_cast<int>(create_status));
            }
        }
    }
}

class EndpointNotificationLogger final : public IMMNotificationClient {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
        if (!out) {
            return E_POINTER;
        }
        *out = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ref_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        return ref_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR device_id, DWORD state) override
    {
        Log::Warn("Audio device callback state_changed id=%s state=0x%08lX",
                  device_id ? Narrow(device_id).c_str() : "<null>", static_cast<unsigned long>(state));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR device_id) override
    {
        Log::Warn("Audio device callback added id=%s", device_id ? Narrow(device_id).c_str() : "<null>");
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR device_id) override
    {
        Log::Error("Audio device callback removed/disconnected id=%s", device_id ? Narrow(device_id).c_str() : "<null>");
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR device_id) override
    {
        Log::Warn("Audio device callback default_changed flow=%d role=%d id=%s",
                  static_cast<int>(flow), static_cast<int>(role),
                  device_id ? Narrow(device_id).c_str() : "<null>");
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR device_id, const PROPERTYKEY key) override
    {
        Log::Info("Audio device callback property_changed id=%s fmtid=%s pid=%lu",
                  device_id ? Narrow(device_id).c_str() : "<null>",
                  GuidToString(key.fmtid).c_str(), static_cast<unsigned long>(key.pid));
        return S_OK;
    }

private:
    std::atomic<ULONG> ref_{1};
};

EndpointNotificationLogger g_endpoint_notification_logger;

void RegisterEndpointNotificationsFromReturnedInterface(void* returned_interface)
{
    if (!returned_interface) {
        return;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    const HRESULT query_hr = reinterpret_cast<IUnknown*>(returned_interface)->QueryInterface(
        IID_IMMDeviceEnumerator,
        reinterpret_cast<void**>(&enumerator)
    );
    Log::Info("Endpoint notification QueryInterface IID_IMMDeviceEnumerator result=0x%08lX requested_interface=%p enumerator=%p",
              static_cast<unsigned long>(query_hr), returned_interface, enumerator);
    if (FAILED(query_hr) || !enumerator) {
        if (enumerator) {
            enumerator->Release();
        }
        return;
    }

    HookIMMDeviceEnumerator(enumerator);
    if (!g_config.diagnostics_enabled || !g_self_reference ||
        g_teardown_requested.load(std::memory_order_acquire)) {
        if (g_config.diagnostics_enabled && !g_self_reference) {
            Log::Error("Endpoint notification registration refused because DLL self-reference is unavailable");
        }
        enumerator->Release();
        return;
    }
    AcquireSRWLockExclusive(&g_endpoint_registration_lock);
    if (g_endpoint_registration_claimed || g_teardown_requested.load(std::memory_order_acquire)) {
        ReleaseSRWLockExclusive(&g_endpoint_registration_lock);
        enumerator->Release();
        return;
    }
    g_endpoint_registration_claimed = true;

    const HRESULT hr = enumerator->RegisterEndpointNotificationCallback(&g_endpoint_notification_logger);
    Log::Info("IMMDeviceEnumerator::RegisterEndpointNotificationCallback result=0x%08lX callback=%p",
              static_cast<unsigned long>(hr), &g_endpoint_notification_logger);
    if (FAILED(hr)) {
        g_endpoint_registration_claimed = false;
        ReleaseSRWLockExclusive(&g_endpoint_registration_lock);
        enumerator->Release();
        Log::Error("Device notification registration failed result=0x%08lX", static_cast<unsigned long>(hr));
        return;
    }

    g_retained_endpoint_enumerator = enumerator;
    ReleaseSRWLockExclusive(&g_endpoint_registration_lock);
    Log::Info("Endpoint notification enumerator retained enumerator=%p ownership=query_interface_reference", enumerator);
}

void UnregisterEndpointNotifications()
{
    AcquireSRWLockExclusive(&g_endpoint_registration_lock);
    IMMDeviceEnumerator* enumerator = g_retained_endpoint_enumerator;
    g_retained_endpoint_enumerator = nullptr;
    g_endpoint_registration_claimed = false;
    ReleaseSRWLockExclusive(&g_endpoint_registration_lock);

    if (!enumerator) {
        Log::Info("Endpoint notification unregistration skipped registered=0");
        return;
    }

    const HRESULT hr = enumerator->UnregisterEndpointNotificationCallback(&g_endpoint_notification_logger);
    Log::Info("IMMDeviceEnumerator::UnregisterEndpointNotificationCallback result=0x%08lX callback=%p enumerator=%p",
              static_cast<unsigned long>(hr), &g_endpoint_notification_logger, enumerator);
    if (FAILED(hr)) {
        Log::Error("Device notification unregistration failed result=0x%08lX", static_cast<unsigned long>(hr));
    }
    enumerator->Release();
    Log::Info("Endpoint notification enumerator released enumerator=%p", enumerator);
}

HRESULT WINAPI HookedCoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD clsctx, REFIID iid, LPVOID* out)
{
    const HRESULT result = g_co_create_instance(clsid, outer, clsctx, iid, out);

    if (IsEqualGUID(clsid, kClsidMMDeviceEnumerator) || IsEqualIID(iid, kIidIMMDeviceEnumerator) || IsAudioClientIid(iid)) {
        g_saw_wasapi_activity = true;
        Log::Info("CoCreateInstance clsid=%s iid=%s clsctx=0x%08lX result=0x%08lX instance=%p",
                  GuidToString(clsid).c_str(),
                  GuidToString(iid).c_str(),
                  static_cast<unsigned long>(clsctx),
                  static_cast<unsigned long>(result),
                  out ? *out : nullptr);
    }

    if (SUCCEEDED(result) && out && *out && IsEqualGUID(clsid, kClsidMMDeviceEnumerator)) {
        RegisterEndpointNotificationsFromReturnedInterface(*out);
    }

    return result;
}

void HookCoCreateInstance()
{
    if (!g_config.wasapi_enabled || g_hooked_co_create_instance.load()) {
        return;
    }

    HMODULE module = GetModuleHandleW(kOle32.data());
    if (!module) {
        return;
    }

    void* target = reinterpret_cast<void*>(GetProcAddress(module, "CoCreateInstance"));
    if (!target) {
        Log::Error("ole32.dll loaded without CoCreateInstance export");
        return;
    }

    const MH_STATUS create_status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedCoCreateInstance),
                                                  reinterpret_cast<void**>(&g_co_create_instance));
    if (create_status != MH_OK) {
        Log::Error("MH_CreateHook failed for CoCreateInstance: %d", static_cast<int>(create_status));
        return;
    }

    const MH_STATUS enable_status = MH_EnableHook(target);
    if (enable_status != MH_OK && enable_status != MH_ERROR_ENABLED) {
        Log::Error("MH_EnableHook failed for CoCreateInstance: %d", static_cast<int>(enable_status));
        return;
    }

    g_hooked_co_create_instance = true;
    Log::Info("Hooked CoCreateInstance in %s", Narrow(kOle32).c_str());
}

[[noreturn]] void ControlledRuntimeTeardown(bool minhook_initialized)
{
    Log::Info("Controlled runtime teardown requested outside DllMain active_spatial_streams=%llu",
              static_cast<unsigned long long>(g_active_spatial_streams.load(std::memory_order_acquire)));
    while (g_active_spatial_streams.load(std::memory_order_acquire) != 0) {
        Sleep(100);
    }

    UnregisterEndpointNotifications();

    if (minhook_initialized) {
        const MH_STATUS disable_status = MH_DisableHook(MH_ALL_HOOKS);
        Log::Info("MH_DisableHook all during controlled teardown status=%d", static_cast<int>(disable_status));
        const MH_STATUS uninitialize_status = MH_Uninitialize();
        Log::Info("MH_Uninitialize during controlled teardown status=%d", static_cast<int>(uninitialize_status));
    }

    Log::Info("Logger shutdown beginning outside loader lock");
    bool logger_stopped = Log::Shutdown(5000);
    if (!logger_stopped) {
        OutputDebugStringA("MetaphorCompleteAudioPatch: logger shutdown exceeded 5 seconds; waiting outside loader lock before teardown can complete.\n");
        logger_stopped = Log::Shutdown(INFINITE);
    }

    HANDLE shutdown_event = g_runtime_shutdown_event.exchange(nullptr, std::memory_order_acq_rel);
    if (shutdown_event) {
        CloseHandle(shutdown_event);
    }

    if (!logger_stopped) {
        OutputDebugStringA("MetaphorCompleteAudioPatch: controlled teardown refused DLL unload because logger termination was not confirmed.\n");
        ExitThread(1);
    }

    OutputDebugStringA("MetaphorCompleteAudioPatch: controlled teardown complete; DLL self-reference retained until process exit.\n");
    ExitThread(0);
}

DWORD WINAPI MainThread(void*)
{
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&MainThread),
            &g_self_reference)) {
        OutputDebugStringA("MetaphorCompleteAudioPatch: failed to retain DLL self-reference; diagnostics bootstrap aborted.\n");
        return 0;
    }
    g_runtime_shutdown_event.store(CreateEventW(nullptr, TRUE, FALSE, nullptr), std::memory_order_release);
    if (!g_runtime_shutdown_event.load(std::memory_order_acquire)) {
        OutputDebugStringA("MetaphorCompleteAudioPatch: failed to create controlled-shutdown event; diagnostics bootstrap aborted.\n");
        FreeLibraryAndExitThread(g_self_reference, 1);
    }

    const std::wstring module_path = GetModuleFileNameString(g_this_module);
    g_module_dir = std::filesystem::path(module_path).parent_path();

    g_config = LoadConfig(g_module_dir / kConfigName);
    QueryPerformanceFrequency(&g_qpc_frequency);
    std::filesystem::path log_path = g_config.diagnostics_log_path;
    if (log_path.empty()) {
        log_path = L"MetaphorCompleteAudioPatch.log";
    }
    if (log_path.is_relative()) {
        log_path = g_module_dir / log_path;
    }
    Log::Init(
        log_path,
        g_config.diagnostics_enabled,
        g_config.recovery_logging,
        static_cast<std::uint64_t>(g_config.diagnostics_max_log_size_mb) * 1024ULL * 1024ULL,
        g_config.diagnostics_max_log_files
    );

    Log::Info("%s loaded from %s", Narrow(kFixName).c_str(), Narrow(g_module_dir.wstring()).c_str());
    Log::Info("Config: xaudio2_enabled=%d force_stereo_mastering_voice=%d override_explicit_multichannel_voices=%d",
              g_config.xaudio2_enabled,
              g_config.force_stereo_mastering_voice,
              g_config.override_explicit_multichannel_voices);
    Log::Info("Config: wasapi_enabled=%d force_stereo_mix_format=%d force_stereo_is_format_supported=%d force_stereo_initialize=%d",
              g_config.wasapi_enabled,
              g_config.force_stereo_mix_format,
              g_config.force_stereo_is_format_supported,
              g_config.force_stereo_initialize);
    Log::Info("Config: disable_spatial_audio_client=%d reject_multichannel_is_format_supported=%d reject_multichannel_initialize=%d",
              g_config.disable_spatial_audio_client,
              g_config.reject_multichannel_is_format_supported,
              g_config.reject_multichannel_initialize);
    Log::Info("Config: spatial_wrapper_enabled=%d", g_config.spatial_wrapper_enabled);
    Log::Info("Config: diagnostics_enabled=%d log_path=%s max_log_size_mb=%d max_log_files=%d",
              g_config.diagnostics_enabled,
              Narrow(log_path.wstring()).c_str(),
              g_config.diagnostics_max_log_size_mb,
              g_config.diagnostics_max_log_files);
    Log::Info("Config: watchdog observation_only=1 stall_timeout_ms=%d poll_interval_ms=%d non_silent_window_ms=%d buffer_activity_window_ms=%d periodic_status_ms=%d",
              g_config.watchdog_stall_timeout_ms,
              g_config.watchdog_poll_interval_ms,
              g_config.watchdog_non_silent_window_ms,
              g_config.watchdog_buffer_activity_window_ms,
              g_config.diagnostics_periodic_status_ms);
    Log::Info("Config: detailed_buffer_logging=%d buffer_log_sample_rate=%d error_reminder_ms=%d",
              g_config.diagnostics_detailed_buffer_logging,
              g_config.diagnostics_buffer_log_sample_rate,
              g_config.diagnostics_error_reminder_ms);
    Log::Info("Config: recovery_enabled=%d adaptive_buffer_retry=%d reset_restart_fallback=%d recreate_client_fallback=%d maximum_attempts_per_failure=%d maximum_recoveries_per_window=%d recovery_window_ms=%d recovery_cooldown_ms=%d fault_inject_buffer_too_large_after=%d",
              g_config.recovery_enabled,
              g_config.recovery_adaptive_buffer_retry,
              g_config.recovery_reset_restart_fallback,
              g_config.recovery_recreate_client_fallback,
              g_config.recovery_maximum_attempts_per_failure,
              g_config.recovery_maximum_recoveries_per_window,
              g_config.recovery_window_ms,
              g_config.recovery_cooldown_ms,
              g_config.recovery_fault_inject_buffer_too_large_after);
    Log::RecoveryWarn("RECOVERY_CONFIGURATION enabled=%d adaptive_buffer_retry=%d reset_restart_fallback=%d recreate_client_fallback=%d maximum_attempts_per_failure=%d maximum_recoveries_per_window=%d recovery_window_ms=%d recovery_cooldown_ms=%d fault_injection_compiled=%d",
                      g_config.recovery_enabled,
                      g_config.recovery_adaptive_buffer_retry,
                      g_config.recovery_reset_restart_fallback,
                      g_config.recovery_recreate_client_fallback,
                      g_config.recovery_maximum_attempts_per_failure,
                      g_config.recovery_maximum_recoveries_per_window,
                      g_config.recovery_window_ms,
                      g_config.recovery_cooldown_ms,
                      METAPHOR_COMPLETE_AUDIO_PATCH_ENABLE_FAULT_INJECTION);
    if (g_config.recovery_recreate_client_fallback) {
        Log::Warn("RecreateClientFallback requested but not implemented; option ignored");
    }
    Log::Info("Config: module_poll_timeout_ms=%d module_poll_interval_ms=%d",
              g_config.module_poll_timeout_ms,
              g_config.module_poll_interval_ms);

    const MH_STATUS init_status = MH_Initialize();
    const bool minhook_initialized = init_status == MH_OK || init_status == MH_ERROR_ALREADY_INITIALIZED;
    if (!minhook_initialized) {
        Log::Error("MH_Initialize failed: %d", static_cast<int>(init_status));
    } else {
        const int timeout_ms = g_config.module_poll_timeout_ms;
        const int interval_ms = g_config.module_poll_interval_ms;
        int elapsed_ms = 0;
        while (timeout_ms == 0 || elapsed_ms <= timeout_ms) {
            HookCoCreateInstance();

            if (g_config.xaudio2_enabled) {
                HookXAudio2Export(kXAudio27.data(), true);
                HookXAudio2Export(kXAudio28.data(), false);
                HookXAudio2Export(kXAudio29.data(), false);
                HookXAudio2Export(kXAudio29Redist.data(), false);
            }

            static bool logged_wasapi_activity = false;
            if (g_saw_wasapi_activity.load() && !logged_wasapi_activity) {
                Log::Info("Observed WASAPI activity");
                logged_wasapi_activity = true;
            }

            if (g_hooked_xaudio2_modern.load() || g_hooked_xaudio2_27.load()) {
                Log::Info("At least one XAudio2 module is hooked");
                break;
            }

            Sleep(interval_ms);
            elapsed_ms += interval_ms;
        }

        if (!g_config.xaudio2_enabled && !g_config.wasapi_enabled) {
            Log::Warn("No audio hooks are enabled in config");
        } else if (g_config.xaudio2_enabled && !g_hooked_xaudio2_modern.load() && !g_hooked_xaudio2_27.load() && !g_saw_wasapi_activity.load()) {
            Log::Warn("Timed out waiting for XAudio2 or WASAPI activity");
        } else if (!g_hooked_xaudio2_modern.load() && !g_hooked_xaudio2_27.load()) {
            Log::Info("Bootstrap loop ended without additional audio activity");
        }
    }

    Log::Info("Runtime coordinator waiting for explicit controlled-shutdown request");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    const DWORD wait_result = WaitForSingleObject(g_runtime_shutdown_event.load(std::memory_order_acquire), INFINITE);
    if (wait_result == WAIT_OBJECT_0) {
        ControlledRuntimeTeardown(minhook_initialized);
    }
    Log::Error("Runtime coordinator shutdown wait failed result=%lu win32_error=%lu", wait_result, GetLastError());
    return 0;
}
}

extern "C" __declspec(dllexport) BOOL WINAPI RequestMetaphorCompleteAudioPatchDiagnosticShutdown()
{
    HANDLE shutdown_event = g_runtime_shutdown_event.load(std::memory_order_acquire);
    if (!shutdown_event) {
        return FALSE;
    }
    g_teardown_requested.store(true, std::memory_order_release);
    return SetEvent(shutdown_event);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t exe_path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
        if (wcsstr(exe_path, L"crash_handler.exe") != nullptr) {
            return FALSE;
        }

        DisableThreadLibraryCalls(module);
        g_this_module = module;

        HANDLE thread = CreateThread(nullptr, 0, &MainThread, nullptr, CREATE_SUSPENDED, nullptr);
        if (thread) {
            // This thread only installs hooks and polls for late-loaded audio
            // modules. Giving it real-time priority can preempt the game's
            // render/loading work each time the polling loop wakes.
            SetThreadPriority(thread, THREAD_PRIORITY_NORMAL);
            ResumeThread(thread);
            CloseHandle(thread);
        }
    }

    return TRUE;
}
