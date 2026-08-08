# queryCreator — instructions for Claude Code

## Tests are mandatory for any new functionality

**Any new functionality must be covered by a test in the same change that
adds it** — not postponed and not split into a separate task. This applies
not only to entirely new code, but also to changes to existing code (see
the detailed plan and coverage priority in
[docs/testing.md](docs/testing.md)).

How to determine whether a test is needed right now:
- A new enum value (`compareTypes`/`functionTypes`/`dataTypes` in
  `qcsqlbase.h`) with not a single line of code that handles it —
  nothing to test, a test is not needed yet.
- As soon as code appears that actually interprets that value —
  a new branch in `if constexpr`/`switch`/`if`-chain, a new method, a new
  execution path — a test is needed immediately, in the same change. Example:
  the branch for `std::vector<std::byte>` in `toParamText`
  (`qcnativeconnection.cpp`) — this is already behavior, not just a type
  declaration, and should have been covered by a test at the time of
  addition.

Before considering a task complete: verify that every new public
method/behavioral branch has a test that would fail if that behavior were
broken.

Technical details of the test harness (GoogleTest/ctest, how to run,
current coverage map) — in [docs/testing.md](docs/testing.md).
