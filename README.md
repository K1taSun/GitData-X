# GitData X

Status: In development (WIP). `gitdatax_core` and the `src/git/` ODB layer are implemented and tested; the `src/tui/` layer is still being integrated (interactive date-parameter modals and `[Space]` multi-select are not yet wired in `AppController`).

GitData X is a C++23 TUI tool for zero-working-tree-touch Git history rewriting: editing commit timestamps and splitting commits (`1 -> K`) directly in the Git Object Database (ODB) via `libgit2` without modifying the working tree or `.git/index`.

## Architecture

| CMake Target | Sources | Deps | Scope |
|---|---|---|---|
| `gitdatax_core` (static) | `src/core/`, `src/git/` | `libgit2` | Immutable DAG model, Kahn sort, time strategies, `1 -> K` splitter, formal verifier, CAS refs, ODB I/O. No FTXUI dependency. |
| `gitdatax` (bin) | `src/main.cpp`, `src/tui/` | `gitdatax_core`, `ftxui` | `AppController`, `DagCanvas`, `SplitWizard`, `DateModals`, `Theme2026`. |
| `gitdatax_tests` (bin) | `tests/` | `gitdatax_core`, `Catch2`, `rapidcheck` | Unit, integration, and property-based tests (no terminal needed). |

Design invariants:
- No `libgit2` pointers outside `src/git/`: `DagSnapshot` stores value-type `CommitNode`s (`std::int64_t` timestamps, parent order preserved for `--first-parent`/octopus merges, `HeadKind::{Unborn,Symbolic,Detached}`) and is `const`/thread-safe without mutexes.
- Exception-free error model: `GitResult<T> = std::expected<T, GitError>`; `libgit2` handles wrapped in `std::unique_ptr` custom deleters (`src/git/raii.hpp`) and `GitLibInit` RAII guard.
- Read-only guard: `Repo::is_read_only()` blocks writes when `RepoState` is `Merge`, `Revert`, `CherryPick`, `Bisect`, `Rebase`, or `ApplyMailbox`.

## Rewrite Pipeline (`RewriteEngine::execute`)

1. Guard & scope: abort if `repo_.is_read_only()`; resolve `RewritePlanner::affected_branches()`.
2. Checkpoint (`BackupManager`): snapshot `HEAD` and target branches under `refs/gitdatax/backup/<YYYYMMDD_HHMMSS_mmm>/` (protects old objects from `git gc`).
3. Deterministic Kahn sort: `DagSnapshot::topological_sort_oldest_first()` orders ancestors before descendants (ties broken by `committer.timestamp`, then `Oid`).
4. ODB rebuild (`chain_map`):
   - `1 -> 1` / `CascadeShaOnly`: re-parent to `chain_map[p].back()` and write via `git_commit_create_from_ids` keeping original `tree_id`.
   - `Split 1 -> K` (`CommitSplitter` + `InMemorySplitter`): distribute diffs (`Uniform` or `SemanticHeadersSourceTests`), synthesize intermediate trees in RAM (`git_index_new` -> `git_index_read_tree` -> `git_blob_create_from_buffer` -> `git_index_write_tree_to`), and assign `orig->tree_id` + original timestamp to step `K - 1`.
5. Formal verification (`DagVerifier::verify`):
   - Vertex balance: `sum(|M(c)|) == |V_new|` with `K_c >= 1`.
   - Boundary Tree Invariant: `tree_id(M(c).back()) == tree_id(c)`.
   - Chain integrity: non-zero `tree_id` on every link; link `j > 0` has single parent `M(c)[j - 1]`.
   - Edge isomorphism: `M(c).front()` parents equal `[M(p).back() for p in parents(c)]` in exact order.
6. Atomic CAS commit (`RefTransaction`): `git_reference_peel` CAS check (returns `GIT_EMODIFIED` on external race), `git_transaction_lock_ref` + `git_transaction_set_target` + `git_transaction_commit`, followed by explicit `HEAD` sync (`git_repository_set_head` / `_detached`).

## Time Strategies (`ITimeStrategy`)

Deterministic via `std::mt19937_64` with `DateTarget` (`AuthorOnly`, `CommitterOnly`, `Both`):

| # | Class | Mechanism |
|---|---|---|
| 1 | `AbsolutePresetStrategy` | Fixed ISO-8601 timestamp, natural presets (`now`, `yesterday HH:MM[:SS]`), or `parent [+|- <delta>]`. |
| 2 | `ConstantDeltaShiftStrategy` | Uniform shift (`+/- delta`); preserves exact inter-commit intervals. |
| 3 | `RangeJitterStrategy` | Linear interpolation over `[t_start, t_end]` with bounded jitter and strict monotonicity. |
| 4 | `WorkHoursStrategy` | Clamps commits to workday window (default `09:00-17:00`, Mon-Fri) with day-overflow rollover. |
| 5 | `SyncAndHealStrategy` | Signature sync (`CommitterToAuthor` / `AuthorToCommitter`), DAG chronology auto-heal (`+60s` min step), TZ conversion (`PreserveInstantUtc` / `PreserveClockTime`). |
| 6 | `PiecewiseRandomStrategy` | Splits into sessions (by `auto_session_gap_sec`, day change, or manual breaks) and runs a bounded random walk hitting `target_full_date` to the exact second. |

## Build & Test

Requirements: C++23 compiler (MSVC 2022, GCC 13+, Clang 16+), CMake 3.21+, Ninja, `vcpkg` (`VCPKG_ROOT` set; dependencies in `vcpkg.json`: `libgit2`, `ftxui`, `catch2`, `rapidcheck`).

```powershell
cmake --preset default
cmake --build build
ctest --test-dir build --output-on-failure
```

- AddressSanitizer: `cmake --preset default -DGITDATAX_ENABLE_ASAN=ON`
- Windows MSVC helper: `.\scripts\build.ps1` (loads `vcvars64.bat`, configures, builds, runs `ctest`)

## CLI & TUI Keybindings

`gitdatax [REPO_PATH] [-v|--version] [-h|--help]` (defaults to `Repo::open_from_cwd()`).

| Key | Action (`AppController`) |
|---|---|
| `Up` / `k`, `Down` / `j` | Navigate topologically sorted commits |
| `1`-`6` | Select active time strategy mode |
| `R` / `r` | Reroll PRNG seed |
| `s` | Plan `1 -> K` split on selected commit (default `split_K = 3`, non-merge only) |
| `p` | Execute `RewriteEngine::execute` and reload `DagSnapshot` |
| `u` | Restore latest checkpoint (`BackupManager::restore_latest()`) |
| `q` / `Esc` | Exit |
