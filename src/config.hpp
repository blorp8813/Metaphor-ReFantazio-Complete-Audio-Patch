#pragma once

#include "stdafx.h"

struct Config {
    bool xaudio2_enabled = true;
    bool force_stereo_mastering_voice = true;
    bool override_explicit_multichannel_voices = true;
    bool wasapi_enabled = true;
    bool force_stereo_mix_format = true;
    bool force_stereo_is_format_supported = true;
    bool force_stereo_initialize = false;
    bool disable_spatial_audio_client = false;
    bool reject_multichannel_is_format_supported = true;
    bool reject_multichannel_initialize = true;
    bool spatial_wrapper_enabled = true;
    bool diagnostics_enabled = false;
    std::filesystem::path diagnostics_log_path;
    int diagnostics_max_log_size_mb = 16;
    int diagnostics_max_log_files = 3;
    int watchdog_stall_timeout_ms = 2000;
    int watchdog_poll_interval_ms = 250;
    int watchdog_non_silent_window_ms = 750;
    int watchdog_buffer_activity_window_ms = 750;
    int diagnostics_periodic_status_ms = 1000;
    bool diagnostics_detailed_buffer_logging = false;
    int diagnostics_buffer_log_sample_rate = 100;
    int diagnostics_error_reminder_ms = 30000;
    int module_poll_timeout_ms = 120000;
    int module_poll_interval_ms = 250;
};

Config LoadConfig(const std::filesystem::path& path);
