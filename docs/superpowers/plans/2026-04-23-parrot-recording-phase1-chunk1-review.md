## Plan Review - Chunk 1

**Status:** Issues Found

**Issues (if any):**
- [Task 2, Step 3]: Missing dual‑buffer pipeline as described in spec 2.2.2 – current implementation uses a single buffer and processes chunks, which may affect streaming performance.
- [Task 3, Step 3]: FFT implementation uses a simple O(N²) DFT with a note to “consider using ESP‑DSP”; this is a performance risk for real‑time detection. Should be flagged as a known limitation.
- [Task 3, Step 3]: Hard‑coded `SAMPLE_RATE 16000` conflicts with `config.sample_rate` variable; peak‑frequency calculation uses `config.sample_rate` but the FFT size and bin mapping assume 16 kHz.
- [Task 4, Step 1]: Kconfig menu omits `PARROT_RECORDING_BUFFER_SIZE_MS` and `PARROT_RECORDING_WS_PORT` (listed in spec 3.1). While they may be deferred to later phases, their absence creates a spec‑plan mismatch.
- [Task 1‑4, various steps]: Verification commands hard‑code `COM3`; should use a placeholder (e.g., `COMx` or `PORT`) to avoid confusion on different developer machines.

**Recommendations (advisory):**
- Add a brief note in Task 2 that the dual‑buffer pipeline will be implemented in Phase 2 (Transport & Protocol).
- In Task 3, replace the simple DFT with a call to ESP‑DSP’s FFT functions (if available) or at least document that this is a temporary implementation for Phase 1.
- Update `SAMPLE_RATE` define to use `config.sample_rate` or remove the define altogether.
- Consider adding the missing Kconfig options with default values, even if they are not used in Phase 1, to keep the configuration complete.
- Change `COM3` to `PORT` in all verification steps and add a comment that the user must substitute their actual serial port.