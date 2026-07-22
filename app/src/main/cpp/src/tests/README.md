# Native Test Layout

- `suites/`: deterministic native self-checks compiled into the demo library when `NYX_ENABLE_NATIVE_TESTS` is on.
- `integration/`: optional gates that may need device, network, or credential configuration.
- `probes/`: symbols built into `libnyx_test_probe.so` for runtime loader, hook, VFS, and Unity checks.
- `support/`: shared test helpers and test-only accessors used by the suites.
