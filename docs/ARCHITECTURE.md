# Architecture

## Audio path

The ASI loader starts `MetaphorCompleteAudioPatch.asi` in the game process. MinHook intercepts selected XAudio2 exports and COM activation. The spatial wrapper presents the API expected by the game, creates an underlying stereo WASAPI render client, accepts the game's static spatial objects, and mixes those object buffers into the acquired stereo render buffer.

`SpatialRenderStream::BeginUpdatingAudioObjects` acquires a render buffer for one processing period. `EndUpdatingAudioObjects` mixes only the acquired frame count and releases that same count. Stream state and the object list are protected by the stream critical section; high-frequency diagnostic state uses atomics.

## Adaptive recovery

On `AUDCLNT_E_BUFFER_TOO_LARGE`, the coordinator reads current padding, validates `padding <= capacity`, computes `available = capacity - padding`, and retries once with `min(period, available)`. Success returns the actual acquired frame count to the mixer. The next callback requests the normal full processing period again.

The production path does not call `Stop`, `Reset`, or `Start`, does not recreate the client, and never loops a failed processing pass. Reset/restart fallback code is retained for research but disabled in production. Client recreation is not implemented.

## Diagnostics and logging

Full diagnostics collect audio-clock, padding, callbacks, state transitions, endpoint notifications, and aggregate render counters. The watchdog is observation-only. It distinguishes clock stalls with continued submissions from callback starvation after active non-silent playback.

The logger uses a fixed-capacity queue and a writer thread. Producers use try-lock operations and drop records instead of blocking. Shutdown disables producers, crosses an SRW-lock gate, drains the writer, and closes handles only after confirmed thread termination. Controlled shutdown occurs on the runtime teardown thread, not under `DllMain` or the loader lock.

Production `RecoveryLogging` uses the same bounded writer but admits only recovery configuration, success, failure, and circuit-breaker events when full diagnostics are disabled. Raw audio samples are never logged.

## COM endpoint notifications

The hook queries the returned COM object for `IMMDeviceEnumerator` rather than assuming the requested interface. A retained enumerator owns callback registration. Controlled teardown unregisters the callback and releases ownership before unload can become possible; blocking COM teardown is excluded from `DllMain`.

## Uncertainties

Wine and CrossOver implement WASAPI, COM, timing, and event behavior differently from Windows. Callback cadence, padding semantics, clock correlation, and endpoint notifications may vary by engine and version. The current recovery is deliberately limited to the confirmed buffer-size failure.
