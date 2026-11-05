# FinTally Feedback — authenticated IPC and Slint UI

## Phase 1 scope

The feedback backend and SQL migrations are already in the workspace. This phase wires
feedback into the existing `kinetib_ui` Slint application without introducing a second
network API or giving the UI direct database access.

The request path is:

```text
Slint FeedbackPage
    -> native/browser UI controller
    -> kinetib_ui backend adapter
    -> kinetib_ipc_client + kinetib_ipc_protocol
    -> authenticated kinetib_kernel command
    -> kinetib_app::feedback
    -> kinetib_db::feedback
```

The trusted authenticated user identity comes from the IPC session. The UI does not send a
user ID, session token, database URL, or direct SQL statement.

## UI operations present in the current backend contract

- Submit authenticated feedback.
- Select `bug_report`, `feature_request`, `general`, or `support`.
- Select an optional affected feature from the domain allowlist.
- Enter an optional title and required message.
- Select an optional 1–5 experience rating.
- Provide contact name/email only after explicitly enabling contact consent. The domain
  requires a valid email whenever contact consent is enabled.
- Enable optional diagnostics consent. The current UI sends only a coarse device class;
  it does not collect OS version or locale.
- Show a success/duplicate receipt and its submission UUID.
- Preserve an ambiguous submission in memory and retry the same UUID and payload. After
  a dropped connection, reauthentication must use the same login email. Explicit logout,
  account switch, LocalOnly sign-in, or discarding the retry clears the pending request.

The current IPC contract does **not** expose feedback-history listing or status mutation.
The UI therefore does not pretend to show a history list or let a user change `new`,
`triaged`, `in_progress`, `resolved`, or `closed` status. Those statuses remain backend
workflow fields until a read/management command is added deliberately.

## Privacy and validation

- Feedback is a CloudSync operation. LocalOnly mode displays an explanation and disables
  submission.
- Message validation is bounded at 5,000 Unicode scalar values and 6,000 UTF-8 bytes.
  The Slint edit callbacks also truncate retained message text at those limits as the user
  types or pastes. A platform text widget can transiently allocate for the incoming paste
  event before the callback clamps the retained value; this is not an OS-wide hard RSS cap.
- Title is clamped to 120 characters/bytes while editing and checked again by domain validation.
- Contact name is clamped to 120 bytes and contact email to 254 bytes while editing.
- Contact data is discarded from the request unless contact consent is enabled.
- Diagnostics are absent unless diagnostics consent is enabled. The UI currently submits
  only `device_class`; `os_version` and `locale` are `None`.
- Feedback content is never written to UI logs or included in error messages.
- A retry request is held in process memory only. It is not persisted to browser storage or
  disk. It is cleared on logout or when another account is selected.

## Memory, framing, ABI and performance rules

`kinetib_ipc_protocol` caps one encoded request payload at 16 KiB and the complete IPC
frame at 65,536 bytes (including its 16-byte header). The UI includes a test that serializes
a maximum-size valid feedback request and asserts it fits the request-payload limit.

The feedback request fields are bounded well below the 64 KiB frame ceiling. The 64 KiB
feature working-set target is a design budget, not a measured peak-RSS claim: Slint's own
runtime, renderer, font cache, transport runtime and OS allocations are outside this
feature-owned budget. Peak allocation measurement on each target remains required before
claiming a hard 64 KiB total-memory guarantee.

No `repr(C)` or cache-line padding is added to the feedback DTOs: they cross a Serde/JSON
IPC boundary, not a C ABI or hot shared-memory ring. Adding C layout or 64-byte alignment
would not improve this request/response path. The existing fixed-size IPC frame and bounded
codec are the applicable controls.

Local validation and serialization are expected to be small, but no 3–5 ms or 25–60 µs
latency result is claimed without target-device benchmarks. A network/database submission
cannot be guaranteed to complete within those local-operation targets.

## Platform integration notes

- **Linux/macOS:** native UI uses the existing Unix-domain IPC transport. The current
  default socket is `/tmp/kinetib-kernel.sock`; `KINETIB_IPC_SOCKET_PATH` overrides it.
- **Windows:** native UI uses the existing named-pipe transport. The default is
  `\\.\pipe\kinetib-kernel`; `KINETIB_IPC_PIPE_NAME` overrides it.
- **WebAssembly:** UI uses the existing browser WebSocket transport at
  `/ws/kinetib-ipc`. A server-side gateway must expose the same versioned IPC protocol and
  enforce the frame limit before delivering binary frames.
- **Android/iOS:** Slint UI code is shared, but the app must provide a private app-container
  Unix socket through `KINETIB_IPC_SOCKET_PATH` and host/connect to the kernel service.
  The current native adapter deliberately returns unavailable when that path is not set.
- **Tablet:** the Slint root switches between compact navigation below 720 logical pixels
  and the desktop sidebar at/above that breakpoint. The feedback form itself scrolls.
- **Embedded/smartwatch:** no target build or device-specific layout is included in this
  phase. The feedback domain and IPC request remain reusable, but watch-specific input,
  screen constraints and platform transports are future work.

## Changed files

- `kinetib_ui/src/controller/feedback.rs` — request mapping, validation, retry state,
  native/WebAssembly submission callbacks and unit tests.
- `kinetib_ui/src/controller/native.rs` and
  `kinetib_ui/src/controller/native/budgets.rs` — native controller split; the former
  oversized controller is now below the 1,000-line limit.
- `kinetib_ui/src/controller/browser.rs` — WebAssembly feedback lifecycle integration.
- `kinetib_ui/ui/pages/app/FeedbackPage.slint` — responsive feedback form.
- `kinetib_ui/ui/pages/app/PageArea.slint`, `AppNavigation.slint`, and `main.slint` —
  protected route, desktop/compact navigation, property and callback wiring.
- `kinetib_ui/src/navigation.rs` — typed protected Feedback route.
- `kinetib_ui/src/backend/mod.rs` — explicit ambiguous-result error for authenticated
  feedback.
- `kinetib_ipc_protocol/src/lib.rs` — public re-export of the shared validation error.

## Build and test commands

Run from the full `kinetib_core` workspace:

```bash
cargo fmt --all -- --check
cargo test -p kinetib_ui
cargo test -p kinetib_feedback_domain -p kinetib_ipc_protocol -p kinetib_ipc_client
cargo check -p kinetib_ui --target wasm32-unknown-unknown
```

The WASM build requires the `wasm32-unknown-unknown` target. Android/iOS builds additionally
require their respective Rust targets, platform SDKs and an app-hosted kernel socket.
The source archive used for this integration does not contain every workspace crate and
this working environment did not have `cargo`/`rustc`, so compilation and target builds
must be run in the full developer workspace before merging.
