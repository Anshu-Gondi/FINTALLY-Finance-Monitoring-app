# FinTally Feedback UI — Phase 1 Patch

This is an overlay patch. Extract it from the `kinetib_core` workspace root so
all paths land in their existing crate locations.

```bash
# From ~/projects/KINETIB-Finance-Monitoring-app/kinetib_core
# Replace the source path with wherever you copied the archive.
tar -xzf /path/to/kinetib-feedback-ui-phase1-patch.tar.gz -C .

cargo fmt --all -- --check
cargo test -p kinetib_ui
cargo test -p kinetib_feedback_domain -p kinetib_ipc_protocol -p kinetib_ipc_client
cargo check -p kinetib_ui
cargo check -p kinetib_ui --target wasm32-unknown-unknown
```

The WASM check requires the `wasm32-unknown-unknown` Rust target. Windows,
Android, and iOS builds need their corresponding Rust target and platform SDK.
For Android/iOS, configure the private kernel IPC socket path in the app host;
the current adapter intentionally returns unavailable when it is absent.
The browser build needs the `/ws/kinetib-ipc` gateway to the versioned kernel
IPC protocol.

## Included behavior

- Adds a protected Feedback page and navigation entry.
- Submits through the existing authenticated IPC client; no direct database
  access is introduced in the UI.
- Supports category, affected feature, title, message, optional rating,
  contact consent, and diagnostics consent.
- Retains only bounded input values while editing and validates them again
  before submission.
- Keeps an ambiguous submission's exact request UUID/payload in process memory
  so the same account can retry idempotently after reconnecting.
- Clears a pending retry on logout, explicit local-session clearing, LocalOnly
  login, account change, or user discard.
- Splits native budget-controller wiring out of the former >1,000-line native
  controller.
- Does not add history or status-management UI because those commands are not
  present in the supplied IPC contract.

## Performance and portability notes

The IPC codec limits request payloads to 16 KiB and full frames to 65,536 bytes.
The feedback module includes a test asserting that a maximum valid request fits
the request payload limit. The 64 KiB feature working-set target is a bounded
design budget, not a measured whole-process peak-RSS guarantee. Slint, renderer,
font cache, OS, and transient platform paste allocations are outside that claim.
No `repr(C)` or cache-line padding is added because this DTO crosses a Serde/JSON
IPC boundary, not a C ABI or shared-memory hot path. Latency targets must be
measured on actual target devices; no timing result is claimed by this patch.
