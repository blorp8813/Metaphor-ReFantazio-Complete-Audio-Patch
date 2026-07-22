# Audio buffer recovery prototype

## Scope

This prototype addresses only the confirmed failure where the internal
`IAudioRenderClient::GetBuffer` returns `AUDCLNT_E_BUFFER_TOO_LARGE`. It does
not recreate `IAudioClient`, the game-facing `SpatialRenderStream`, the event
handle, or active spatial objects. Other GetBuffer errors retain their prior
behavior.

Recovery is configured under `[Recovery]`. A missing or malformed `Enabled`
value leaves recovery disabled. `FaultInjectBufferTooLargeAfter` defaults to
zero and malformed values also resolve to zero. The adaptive-retry-only package
sets `Enabled=true`, but reset/restart, client recreation, and fault injection
remain off.

## Buffer geometry

After internal WASAPI initialization, the wrapper obtains and stores the actual
endpoint buffer capacity through `IAudioClient::GetBufferSize`. A failed call
or zero capacity fails stream activation. The processing period remains derived
from `GetDevicePeriod`; the implementation does not assume that period and
capacity are equal. Capacity, period, initial padding, and output format are
logged together.

## Recovery state

1. The normal path calls `GetBuffer(period_frames)` once.
2. Any result except `AUDCLNT_E_BUFFER_TOO_LARGE` is returned unchanged.
3. For `AUDCLNT_E_BUFFER_TOO_LARGE`, the wrapper reads current padding and
   computes `capacity - padding` after validating `padding <= capacity`.
4. If frames are available, it retries with
   `min(period_frames, available_frames)`.
5. A successful adaptive retry becomes the active update size and the original
   failure is not counted as a final GetBuffer failure.
6. Zero availability, inconsistent padding, or a second
   `AUDCLNT_E_BUFFER_TOO_LARGE` can enter one Stop/Reset/Start fallback only
   when `ResetRestartFallback=true`.
7. After Start, padding is refreshed and one bounded GetBuffer retry is made.
8. Success is returned to Metaphor only after a real buffer has been acquired.

The circuit breaker permits at most one fallback attempt for a failed processing
pass, applies the configured cooldown, and permits at most the configured number
of fallback starts in the time window. A fixed 100-entry timestamp array avoids
allocation on the audio recovery path. Full client recreation is explicitly not
implemented.

## Smaller-frame safety

`SpatialRenderObject` storage remains allocated for `period_frames`, the maximum
size exposed by this wrapper. An adaptive retry can only choose a value in
`[1, period_frames]`. The selected value is stored in `update_frames_` and is
used for:

- the frame count returned by `BeginUpdatingAudioObjects`;
- each spatial object's returned byte count;
- clearing object sample storage for the active pass;
- all object-sample reads in the stereo mixing loop;
- all stereo render-buffer writes;
- silence detection; and
- `IAudioRenderClient::ReleaseBuffer`.

No sample beyond `update_frames_` is read or written for the pass. This makes a
smaller per-pass count safe with the current object implementation, assuming
Metaphor honors the frame count returned by `BeginUpdatingAudioObjects`, as the
spatial-audio contract requires.

## Synchronization and locks

The stream's `CRITICAL_SECTION lock_` is held during:

- the original GetBuffer and any adaptive retry;
- GetCurrentPadding used by recovery;
- the fallback Stop, Reset, Start, refreshed padding, and final GetBuffer;
- Start, Stop, and Reset calls made through the game-facing stream;
- mixing and ReleaseBuffer in `EndUpdatingAudioObjects`; and
- spatial-object list and buffer access already protected by the stream.

The watchdog uses `TryEnterCriticalSection` and skips a poll rather than
overlapping its padding/clock COM calls with recovery. The portable coordinator
also has an atomic recovery gate. `recovery_in_progress_`, protected by
`lock_`, rejects same-thread reentry with
`AUDCLNT_E_BUFFER_OPERATION_PENDING`.

No logger lock is held while any WASAPI method is called. Lightweight,
nonblocking `BUFFER_RECOVERY_STAGE_BEFORE` and `BUFFER_RECOVERY_STAGE_AFTER`
records are queued immediately around each fallback Stop, Reset, Start,
GetCurrentPadding, and GetBuffer call while `lock_` is held. This leaves a
durable last-stage marker if a Wine call blocks. Aggregate recovery outcomes
are emitted after releasing `lock_`.

## Reentrancy and concurrent Stop

Wine's implementations of Stop, Reset, Start, GetCurrentPadding, and GetBuffer
are not expected to synchronously call the game-facing stream methods. They can
signal the preserved event handle, which may wake another game thread. Such a
thread waits on `lock_`; same-thread recursive entry is rejected by
`recovery_in_progress_`. Endpoint notification callbacks use separate objects
and do not acquire the stream lock.

If Metaphor calls Stop from another thread while internal recovery is executing,
Stop waits for `lock_`. Recovery completes or fails first, then the explicit
game Stop runs and its result becomes authoritative. Thus a concurrent game
Stop can intentionally supersede a successful recovery. The prototype does not
suppress or defer a game-requested Stop. As with the underlying spatial-audio
API, callers are still expected not to race Stop against an outstanding
Begin/End update pair; that separate caller-ordering race is not broadened into
client recreation in this prototype.

## Fault injection

With a positive `FaultInjectBufferTooLargeAfter`, the next processing pass after
that many successful acquisitions skips the initial full-period GetBuffer and
injects the same logical error into the common recovery path. It does not obtain,
modify, release, or corrupt a Wine render buffer. An atomic latch limits the
injection to once per stream.

## Events

The prototype emits:

- `FAULT_INJECTION_BUFFER_TOO_LARGE`
- `BUFFER_TOO_LARGE_CAUGHT`
- `ADAPTIVE_BUFFER_RETRY_SUCCEEDED`
- `ADAPTIVE_BUFFER_RETRY_FAILED`
- `BUFFER_RECOVERY_STARTED`
- `BUFFER_RECOVERY_STAGE_BEFORE`
- `BUFFER_RECOVERY_STAGE_AFTER`
- `BUFFER_RECOVERY_RESET_SUCCEEDED`
- `BUFFER_RECOVERY_SUCCEEDED`
- `BUFFER_RECOVERY_FAILED`
- `RECOVERY_CIRCUIT_BREAKER_OPEN`

Recovery logs include original request size, capacity, padding, availability,
retry sizes, each HRESULT, started-state transitions, and elapsed fallback time.
