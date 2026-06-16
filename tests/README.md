# OpenOS Tests

This directory contains host-side tests for OpenOS.

## Unit tests

Run all unit tests from the repository root:

```bash
bash tests/run_unit_tests.sh
```

The runner builds each `tests/unit/test_*.c` file with the shared lightweight
unit-test harness and writes binaries/logs to `target/unit-tests/`.

Current coverage focuses on contracts that are cheap to validate on the host,
such as architecture descriptor constants and packed structure layouts.
