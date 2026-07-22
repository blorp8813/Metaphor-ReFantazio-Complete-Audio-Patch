## Summary

Describe the user-visible change and why it is needed.

## Verification

- [ ] Portable stall-detector tests pass
- [ ] Portable buffer-recovery tests pass
- [ ] Windows x86-64 build passes
- [ ] `-Wall -Wextra -Wpedantic` warning build passes
- [ ] Target-runtime harness was run, or the reason it was unavailable is documented
- [ ] Recovery/state changes include focused tests

## Safety and compatibility

- [ ] No game binaries, saves, logs, personal paths, or credentials are included
- [ ] License and third-party notices remain intact
- [ ] Audio-thread overhead and concurrency assumptions are explained
- [ ] Wine/CrossOver/Proton uncertainties are explained
- [ ] Production reset/restart and recreation fallbacks remain disabled, or explicit evidence and approval are provided
