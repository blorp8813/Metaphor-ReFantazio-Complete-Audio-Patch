#include "config.hpp"

namespace {
std::string Trim(std::string value)
{
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };

    while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }

    while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }

    return value;
}

std::string ToLower(std::string value)
{
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

bool ParseBool(const std::string& value, bool fallback)
{
    const std::string lowered = ToLower(Trim(value));
    if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
        return true;
    }
    if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
        return false;
    }
    return fallback;
}

int ParseInt(const std::string& value, int fallback)
{
    try {
        return std::stoi(Trim(value));
    } catch (...) {
        return fallback;
    }
}
}

Config LoadConfig(const std::filesystem::path& path)
{
    Config config;

    std::ifstream stream(path);
    if (!stream.is_open()) {
        return config;
    }

    std::string current_section;
    std::string line;
    while (std::getline(stream, line)) {
        const auto comment_pos = line.find_first_of(";#");
        if (comment_pos != std::string::npos) {
            line.erase(comment_pos);
        }

        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            current_section = ToLower(Trim(line.substr(1, line.size() - 2)));
            continue;
        }

        const auto equals_pos = line.find('=');
        if (equals_pos == std::string::npos) {
            continue;
        }

        const std::string key = ToLower(Trim(line.substr(0, equals_pos)));
        const std::string value = Trim(line.substr(equals_pos + 1));

        if (current_section == "xaudio2") {
            if (key == "enabled") {
                config.xaudio2_enabled = ParseBool(value, config.xaudio2_enabled);
            } else if (key == "forcestereomasteringvoice") {
                config.force_stereo_mastering_voice = ParseBool(value, config.force_stereo_mastering_voice);
            } else if (key == "overrideexplicitmultichannelvoices") {
                config.override_explicit_multichannel_voices =
                    ParseBool(value, config.override_explicit_multichannel_voices);
            }
        } else if (current_section == "wasapi") {
            if (key == "enabled") {
                config.wasapi_enabled = ParseBool(value, config.wasapi_enabled);
            } else if (key == "forcestereomixformat") {
                config.force_stereo_mix_format = ParseBool(value, config.force_stereo_mix_format);
            } else if (key == "forcestereoisformatsupported") {
                config.force_stereo_is_format_supported =
                    ParseBool(value, config.force_stereo_is_format_supported);
            } else if (key == "forcestereoinitialize") {
                config.force_stereo_initialize = ParseBool(value, config.force_stereo_initialize);
            } else if (key == "disablespatialaudioclient") {
                config.disable_spatial_audio_client = ParseBool(value, config.disable_spatial_audio_client);
            } else if (key == "rejectmultichannelisformatsupported") {
                config.reject_multichannel_is_format_supported =
                    ParseBool(value, config.reject_multichannel_is_format_supported);
            } else if (key == "rejectmultichannelinitialize") {
                config.reject_multichannel_initialize = ParseBool(value, config.reject_multichannel_initialize);
            }
        } else if (current_section == "spatial") {
            if (key == "wrapperenabled") {
                config.spatial_wrapper_enabled = ParseBool(value, config.spatial_wrapper_enabled);
            }
        } else if (current_section == "diagnostics") {
            if (key == "enabled") {
                config.diagnostics_enabled = ParseBool(value, config.diagnostics_enabled);
            } else if (key == "logpath") {
                config.diagnostics_log_path = std::filesystem::path(Trim(value));
            } else if (key == "maxlogsizemb") {
                config.diagnostics_max_log_size_mb = ParseInt(value, config.diagnostics_max_log_size_mb);
            } else if (key == "maxlogfiles") {
                config.diagnostics_max_log_files = ParseInt(value, config.diagnostics_max_log_files);
            } else if (key == "stalltimeoutms") {
                config.watchdog_stall_timeout_ms = ParseInt(value, config.watchdog_stall_timeout_ms);
            } else if (key == "watchdogpollintervalms") {
                config.watchdog_poll_interval_ms = ParseInt(value, config.watchdog_poll_interval_ms);
            } else if (key == "nonsilentwindowms") {
                config.watchdog_non_silent_window_ms = ParseInt(value, config.watchdog_non_silent_window_ms);
            } else if (key == "bufferactivitywindowms") {
                config.watchdog_buffer_activity_window_ms = ParseInt(value, config.watchdog_buffer_activity_window_ms);
            } else if (key == "periodicstatusms") {
                config.diagnostics_periodic_status_ms = ParseInt(value, config.diagnostics_periodic_status_ms);
            } else if (key == "detailedbufferlogging") {
                config.diagnostics_detailed_buffer_logging =
                    ParseBool(value, config.diagnostics_detailed_buffer_logging);
            } else if (key == "bufferlogsamplerate") {
                config.diagnostics_buffer_log_sample_rate = ParseInt(value, config.diagnostics_buffer_log_sample_rate);
            } else if (key == "errorreminderms") {
                config.diagnostics_error_reminder_ms = ParseInt(value, config.diagnostics_error_reminder_ms);
            }
        } else if (current_section == "recovery") {
            if (key == "enabled") {
                config.recovery_enabled = ParseBool(value, config.recovery_enabled);
            } else if (key == "recoverylogging") {
                config.recovery_logging = ParseBool(value, config.recovery_logging);
            } else if (key == "adaptivebufferretry") {
                config.recovery_adaptive_buffer_retry = ParseBool(value, config.recovery_adaptive_buffer_retry);
            } else if (key == "resetrestartfallback") {
                config.recovery_reset_restart_fallback = ParseBool(value, config.recovery_reset_restart_fallback);
            } else if (key == "recreateclientfallback") {
                config.recovery_recreate_client_fallback = ParseBool(value, config.recovery_recreate_client_fallback);
            } else if (key == "maximumattemptsperfailure") {
                config.recovery_maximum_attempts_per_failure =
                    ParseInt(value, config.recovery_maximum_attempts_per_failure);
            } else if (key == "maximumrecoveriesperwindow") {
                config.recovery_maximum_recoveries_per_window =
                    ParseInt(value, config.recovery_maximum_recoveries_per_window);
            } else if (key == "recoverywindowms") {
                config.recovery_window_ms = ParseInt(value, config.recovery_window_ms);
            } else if (key == "recoverycooldownms") {
                config.recovery_cooldown_ms = ParseInt(value, config.recovery_cooldown_ms);
            } else if (key == "faultinjectbuffertoolargeafter") {
#if METAPHOR_COMPLETE_AUDIO_PATCH_ENABLE_FAULT_INJECTION
                config.recovery_fault_inject_buffer_too_large_after =
                    ParseInt(value, config.recovery_fault_inject_buffer_too_large_after);
#else
                // Public builds deliberately compile out fault injection.
                config.recovery_fault_inject_buffer_too_large_after = 0;
#endif
            } else if (key == "faultinjectzeroavailability") {
#if METAPHOR_COMPLETE_AUDIO_PATCH_ENABLE_FAULT_INJECTION
                config.recovery_fault_inject_zero_availability =
                    ParseBool(value, config.recovery_fault_inject_zero_availability);
#else
                config.recovery_fault_inject_zero_availability = false;
#endif
            }
        } else if (current_section == "bootstrap") {
            if (key == "modulepolltimeoutms") {
                config.module_poll_timeout_ms = ParseInt(value, config.module_poll_timeout_ms);
            } else if (key == "modulepollintervalms") {
                config.module_poll_interval_ms = ParseInt(value, config.module_poll_interval_ms);
            }
        }
    }

    if (config.module_poll_timeout_ms < 0) {
        config.module_poll_timeout_ms = 0;
    }
    if (config.module_poll_interval_ms < 50) {
        config.module_poll_interval_ms = 50;
    }
    config.diagnostics_max_log_size_mb = std::clamp(config.diagnostics_max_log_size_mb, 1, 1024);
    config.diagnostics_max_log_files = std::clamp(config.diagnostics_max_log_files, 1, 16);
    config.watchdog_stall_timeout_ms = std::clamp(config.watchdog_stall_timeout_ms, 250, 60000);
    config.watchdog_poll_interval_ms = std::clamp(config.watchdog_poll_interval_ms, 50, 5000);
    config.watchdog_non_silent_window_ms = std::clamp(config.watchdog_non_silent_window_ms, 100, 60000);
    config.watchdog_buffer_activity_window_ms = std::clamp(config.watchdog_buffer_activity_window_ms, 100, 60000);
    config.diagnostics_periodic_status_ms = std::clamp(config.diagnostics_periodic_status_ms, 100, 60000);
    config.diagnostics_buffer_log_sample_rate = std::clamp(config.diagnostics_buffer_log_sample_rate, 1, 100000);
    config.diagnostics_error_reminder_ms = std::clamp(config.diagnostics_error_reminder_ms, 1000, 600000);
    // A single failed processing pass is never allowed to loop recovery.
    config.recovery_maximum_attempts_per_failure = 1;
    config.recovery_maximum_recoveries_per_window =
        std::clamp(config.recovery_maximum_recoveries_per_window, 1, 100);
    config.recovery_window_ms = std::clamp(config.recovery_window_ms, 1000, 600000);
    config.recovery_cooldown_ms = std::clamp(config.recovery_cooldown_ms, 0, 60000);
    config.recovery_fault_inject_buffer_too_large_after =
        std::clamp(config.recovery_fault_inject_buffer_too_large_after, 0, 1000000000);

    return config;
}
