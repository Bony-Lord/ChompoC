# Execution-only performance checker

CI separates compilation from timing:

1. `performance-build` creates the Release `Chompo` binary and uploads it as an artifact.
2. `performance-execution` downloads that already-built binary and runs `run_performance_suite.py` directly.

The timer in `run_performance_suite.py` starts immediately before launching `Chompo` for a `.chmp` case and stops immediately after the process exits. CMake configure, C++ compilation, linking, artifact upload/download and Python setup are not included in any per-case TLE limit.

Local execution-only run after a Release build:

```bash
python tests/performance/run_performance_suite.py \
  --executable build-release/Chompo \
  --cases tests/performance/cases
```
