# Performance measurements

No Blynx executable has completed full release validation yet. There is
**no Blynx vs Chrome/Brave comparison** and no speed or memory advantage
claim. Release GN configuration is optimized/non-component but not a signed,
official PGO release. Do not optimize security checks away.

## Working local harness

`benchmarks/run.py` uses explicitly supplied binaries, isolated temporary profiles,
five repetitions per mode by default, fixed local file workload and Linux `/proc`
sampling. It does not use CDP, personal profiles or remote test sites. Raw output
and checkpoints live inside ignored `.local/benchmarks/`.

```sh
python3 benchmarks/run.py --browser final=/absolute/release/chrome \
  --browser chrome=/usr/bin/google-chrome \
  --configuration final=release --build-config final=/absolute/release/args.gn \
  --repetitions 5 --profile-mode both --timeout 30
```

This measures launch-to-observed-valid-DOM, not native first-window paint or
cold OS-cache startup. “First-run” means empty profile; “warm” means a primed
isolated profile. Summed RSS double-counts shared memory and is not PSS or idle
RAM. Sampled CPU is a lower bound. Headless results do not establish desktop,
GPU, interactive-tab or network performance. Read all caveats in raw JSON.

## Actual baseline this session

Google Chrome 154.0.8037.97 only, local headless workload, 5/5 successful trials in
each mode. Raw: `.local/benchmarks/run-k4b5f136/results.json`.

| Profile mode | Median launch-to-observed-DOM | Range |
|---|---:|---:|
| Empty/first-run | 304 ms | 273–755 ms |
| Warm | 273 ms | 268–337 ms |

Wide first-run variation confirms this busy workstation's noise. These values
are a harness baseline, not a Blynx result or OS-cache-cold measurement.

An earlier Chrome+Brave run was externally interrupted while Brave's headless
dump hung; it did not produce a complete measurement set. Its scoped leftover
processes were stopped. The runner was hardened with cancellation and incremental
checkpoints afterwards. Do not derive a comparison from that incomplete run.

## Still required

Real desktop cold/warm startup (controlled cache methodology), idle PSS/CPU,
reproducible multi-tab set, tab-switch latency, page-load tracing, GPU/process
information and Speedometer with licensed/local fixture where suitable. Repeat
candidate and installed baselines under matching power/load/toolchain conditions;
store raw measurements before a conclusion. Profile Chromium services/UI work
before adding lazy loading, discard policies or rendering optimizations.

## Current session note

No new Blynx benchmark was run in the redesign follow-up. The Chrome-only local
baseline above remains the only performance evidence; do not infer an advantage
from the successful build or smoke tests.
