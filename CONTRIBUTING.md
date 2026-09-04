## Git Commit Message Convention

FINTALLY uses clear, concise, and action-oriented Git commit messages.

Commit messages should describe **what the commit changes**, not the entire implementation process.

Use an **imperative verb** at the beginning of the commit message.

### Preferred Format

```text
<action> <specific change>
```

Examples:

```text
fix undefined TypeError in updateTransaction

update Docker build to include Tesseract dependencies

optimize RAG pipeline file generator
```

### Good Commit Messages

Good commit messages should be:

* Concise
* Specific
* Written in the imperative form
* Focused on one logical change
* Easy to understand from `git log`
* Useful to developers reviewing project history

Examples:

```text
fix undefined TypeError in updateTransaction

update Docker build to include Tesseract dependencies

optimize RAG pipeline file generator

add WebSocket transaction validation

reduce allocations in transaction processing

improve Rust backend error handling

add benchmark for transaction lookup

fix race condition in local cache

reduce memory usage in RAG indexing

add C++ native engine for matrix matching

update React transaction form validation

improve Rust-to-C++ FFI boundary
```

### Avoid Vague Commits

Avoid messages that do not communicate what actually changed:

```text
fix stuff

update code

changes

fixed bug

important update

working now

final changes

more fixes

test

cleanup
```

These messages make project history difficult to understand and make debugging or reviewing previous changes unnecessarily difficult.

### One Logical Change Per Commit

Whenever practical, keep each commit focused on one logical change.

For example, prefer:

```text
fix undefined TypeError in updateTransaction
```

followed by:

```text
add validation for transaction update payload
```

instead of combining unrelated changes into:

```text
fix transaction stuff and update Docker and optimize RAG
```

A commit may contain multiple file changes when those changes are necessary parts of the same logical change.

---

## Commit Categories

FINTALLY does not require a rigid conventional-commit prefix, but the action should make the purpose clear.

Common actions include:

| Action      | Purpose                                                 |
| ----------- | ------------------------------------------------------- |
| `add`       | Introduce new functionality                             |
| `fix`       | Correct incorrect behavior                              |
| `update`    | Update existing functionality/configuration             |
| `improve`   | Improve an existing implementation                      |
| `optimize`  | Improve performance/resource usage                      |
| `reduce`    | Reduce memory, latency, allocations, dependencies, etc. |
| `remove`    | Remove functionality or unnecessary code                |
| `refactor`  | Restructure code without changing intended behavior     |
| `test`      | Add or modify tests/benchmarks                          |
| `document`  | Add or update documentation                             |
| `secure`    | Address a security-related improvement                  |
| `benchmark` | Add or modify performance benchmarks                    |
| `build`     | Modify build, compilation, or packaging infrastructure  |
| `update`    | Update dependencies, configuration, or infrastructure   |

Examples:

```text
add transaction reconciliation engine

fix undefined TypeError in updateTransaction

update Docker build to include Tesseract dependencies

optimize RAG pipeline file generator

reduce allocations in Rust transaction processing

refactor WebSocket authentication handling

secure local database initialization

benchmark transaction lookup performance

build native C++ matrix matcher

document local assistant architecture
```

---

## Performance-Oriented Commits

Because FINTALLY is designed around **low memory usage and low latency**, performance-related commits should identify the optimization clearly.

Prefer:

```text
reduce allocations in transaction processing

optimize RAG pipeline file generator

reduce serialization overhead in WebSocket responses

improve cache locality in vector search

reduce memory usage in local embedding index

optimize C++ matrix matching engine

reduce latency in Rust request orchestration
```

Avoid:

```text
make faster

performance fix

huge optimization

super fast now
```

When a meaningful performance improvement is made, contributors are encouraged to include benchmark results in the commit body, pull request, or associated documentation.

For example:

```text
optimize RAG pipeline file generator

Reduced intermediate allocations and unnecessary file-system
operations during generation.

Benchmark:
- Before: 142 ms
- After:   91 ms
- Improvement: ~35.9%
```

---

## Commit Body

For simple changes, a subject line may be sufficient.

For complex changes, add a body explaining:

* Why the change was required
* What was changed
* Important implementation details
* Performance implications
* Security implications
* Compatibility considerations

Example:

```text
fix undefined TypeError in updateTransaction

The transaction update path could receive an undefined transaction
payload from the WebSocket response handler.

Add explicit validation before accessing transaction fields and return
a controlled error when the payload is invalid.

This prevents the frontend from crashing during transaction updates.
```

---

## Commit History Principle

FINTALLY's Git history should help future contributors understand how the project evolved.

A useful commit message should allow someone looking at:

```bash
git log
```

to understand what happened without opening every changed file.

The goal is:

> **A commit message should explain the change clearly enough that another engineer can understand its purpose from the project history alone.**
