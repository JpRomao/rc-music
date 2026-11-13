# Download conversion and recovery implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans for direct execution; independent UI work may run alongside codec qualification with disjoint files. Steps use checkbox syntax.

**Goal:** Correct the reproduced temporal-M4A conversion failure and add explicit retry/sanitized information copying without replacing the personal installation.

**Architecture:** Keep the authenticated, read-handle-only Media Foundation worker and the existing durable admission/service/binding pipeline. Qualify the temporal correction against a reproducing synthetic input before retaining product changes. Retry is a new bounded admission from a retained trusted source, never replay of a signed URL or reuse of a terminal workspace.

**Tech Stack:** C++20, Win32/Media Foundation, Qt 6/Qt Quick, CMake/Ninja, Qt Test/Catch2.

**Spec:** `docs/superpowers/specs/2026-11-13-download-diagnostics-and-lrclib-selection-design.md`, sections 1 and 2. The user authorized direct execution; LRCLIB is a separate later delivery.

## Global constraints

- Work in the existing isolated `feat/brave-letras-mp3` worktree, base `677de0d`; preserve preexisting work in every other checkout.
- Keep DNS/TLS, pinned artifacts, ACK/drain, NoChild, read-capability input, limits and publication without overwrite.
- Do not silently discard edit timing, rename MP4 as MP3, introduce telemetry, retain signed URLs in checkpoints, install/register components or restart the personal app.
- Real public-source captures are private diagnostic inputs only; never commit them as fixtures. Automated regressions use generated/licensed data without internet.
- Apply proportional validation: focused Reds/Greens and affected consumers; one integrated affected set after the final source changes. No habitual global suite or repeated baselines.
- User approval covers implementation and an isolated test candidate, not promotion/distribution or personal installation changes.

## Review focus

- Nontrivial edit lists must retain intentional trimming/leading silence; a playable MP3 alone is insufficient.
- Truly missing audio, malformed/truncated containers and quota overflow must not become successful conversion.
- Retry double clicks, checkpoint failure, busy/closing owners and restored history without source must never start unauthorized work.
- Multi-item requests and completed/import-failed siblings must not be redownloaded by retry of one failed row.
- Copy text and QML metadata must omit media URLs, credentials, raw remote errors and unnecessary local paths, including adversarial optional text.

### Task 1: Pin the temporal failure and qualify the native path

**Files:** `tests/qt/WindowsMediaFoundationCodecTests.cpp`; generated/licensed fixture utilities under `tests/audio/` if necessary; private evidence under `out/dg/noaudio-fix-20261113/`.

**Interfaces:** Consume existing `runWindowsMediaFoundationCodec(const MediaFoundationWorkerRequest&, const MediaFoundationCodecCallbacks&)`. Produce a deterministic failure assertion, temporal output assertions, and a documented minimal compatible native adjustment. No new public protocol or backend is assumed.

- [ ] Add a synthetic reproduction with valid audio and the failing temporal profile, plus a matched successful control. Observe failure for the intended behavior before product edits.
- [ ] Distinguish missing native audio type versus empty decoded output in a private typed diagnostic if the existing enum is ambiguous. Keep real captures inside the protected subprocess.
- [ ] Test the smallest native reader adjustment on unchanged bytes; if it cannot preserve timing, qualify a bounded remux/normalization alternative before adding dependencies or discarding metadata.
- [ ] Record exact source/fixture identities, commands, results and rulings in this plan's ledger.

### Task 2: Implement the proven temporal correction

**Files:** `src/adapters/WindowsMediaFoundationCodec.cpp`; its header only if the proven implementation requires it; `tests/qt/WindowsMediaFoundationCodecTests.cpp`, `tests/qt/WindowsMediaFoundationConversionTests.cpp`; `cmake/RcMusicMediaFoundation.cmake` only if a focused helper is necessary.

**Interfaces:** Preserve `MediaFoundationWorkerRequest`, existing codec callbacks, `WindowsLocalMediaConverter`, and native MP3 validation. A selected audio stream that cannot decode must not be presented as proof of missing audio.

- [ ] Retain only the smallest correction supported by Task 1; do not blindly hide `edts` or zero `elst`.
- [ ] Green: original reproduction converts, output timing matches expected trim/delay, full MP3 validation passes, input bytes remain unchanged.
- [ ] Run native codec and conversion consumers, including missing-audio, invalid/truncated input, callbacks, input/output/duration limits, handle-only and physical drain tests.
- [ ] Build the changed worker and authenticate its new hash for isolated candidate use; never weaken the old pin comparison.

### Task 3: Retry and sanitized information UI (independent of Tasks 1–2)

**Files:** `include/m0/application/DownloadService.hpp`, `src/application/DownloadService.cpp`, admission controller if needed; `src/adapters/DownloadJobsModel.{hpp,cpp}`; `src/presentation/DownloadApplicationBinding.{hpp,cpp}`; `src/ui/qml/DownloadsPanel.qml`; matching core/Qt/UI tests and documentation.

**Interfaces:** Reuse `DownloadAdmissionController::begin(const DownloadRequest&)`, its matching Saved/poll contract and `DownloadService::snapshot()`. New binding actions use only an exact job ID: `retryDownload(job_id)`, `downloadInformation(job_id)` and `copyDownloadInformation(job_id)` (or a documented equivalent fitting existing conventions). The action does not accept a URL, path or item supplied by QML.

- [ ] Red: one failed item -> new admission -> no execution before Saved; double click/reentry cannot queue duplicate attempts. Cover save failure, restored source-less history, multi-item siblings, import failure and shutdown.
- [ ] Implement bounded trusted session-source lookup and a new request identity. Preserve old job/history and quotas; do not persist origins or replay old signed endpoints.
- [ ] Add eligibility/reason projection and accessible row actions; keep import retry distinct. Add selectable plain-text details and copy feedback.
- [ ] Red/Green whitelist copying of identifiers/state/exact counters with synthetic secret canaries. Do not read or overwrite the personal system clipboard during automated tests; use a suitable composition seam/offscreen clipboard.
- [ ] Build/test the affected service/controller/model/binding/QML targets using `out/product/dl-settings-tests`; archive full logs. Parent uses a different build directory for codec work.

### Task 4: Integrated qualification and private candidate

**Files:** focused test/build evidence and isolated runtime/package under `out/product/`; existing packaging profile/manifests/pins as required by changed executables; user-facing documentation.

**Interfaces:** Consume the unchanged conversion protocol and the new job-ID actions. Production publication/import remain separate acceptance boundaries.

- [ ] Combine affected regressions once after final source changes; format touched C++/QML and build the Release app/worker. Discover actual runner names from CMake/CTest rather than inventing them.
- [ ] Validate the three supplied original captures with the corrected protected worker, preserving edit timing; reuse captures only after hash checks. Exercise publication/import with isolated product composition and state. Report any unavailable browser/native-ingress boundary honestly.
- [ ] Build/authenticate a private candidate with regenerated hashes/pins/receipts, without modifying installed files, registry, library, queue or active downloads.
- [ ] Request one fresh whole-change review covering the review-focus items. Fix important findings with focused Red/Green; do not duplicate unchanged checks.
- [ ] Review diff and evidence; commit only coherent authorized work per repository instructions, no push or history rewrite. Report executed checks, known failures, unexecuted acceptance boundaries and candidate location separately.
