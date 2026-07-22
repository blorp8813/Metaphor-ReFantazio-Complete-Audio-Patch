# Contributing to Metaphor: ReFantazio Complete Audio Patch

Thank you for helping improve this unofficial compatibility project.

Before opening a change:

1. Search existing issues and avoid posting game files, saves, executables, or unredacted personal paths.
2. Keep changes narrowly scoped and preserve the upstream MIT license and all third-party notices.
3. Run `./tests/run-host-tests.sh`, a Windows cross-build, and the warning build described in `docs/REPRODUCIBLE_BUILDS.md`.
4. Add focused tests for state-machine or recovery behavior.
5. Explain Wine/CrossOver uncertainties and any audio-thread cost in the pull request.

Do not enable reset/restart or recreation fallbacks by default without reproducible evidence, target-runtime testing, and explicit review. Never add proprietary game assets or user diagnostic logs to the repository.

By contributing, you agree that your contribution is provided under the repository's MIT License.
