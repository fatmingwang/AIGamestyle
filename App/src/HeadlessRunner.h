#pragma once
#include <QStringList>

// Entry point for `GameStyleAI --headless [--port <n>] [--host <addr>] [--config <path.json>]`.
// Starts a persistent TCP server (does not return until it fails to bind) that accepts one
// JSON command per line per connection and streams back NDJSON job-progress/result events —
// see Doc/Commands_Reference.md §12 for the protocol. `args` is the full argument list starting
// with "--headless". Returns the process exit code (only reached if the server fails to start).
int runHeadless(const QStringList& args);

// Entry point for `GameStyleAI --headless-test [--port <n>] [--host <addr>] [--config <path.json>]`.
// A one-process smoke test: starts the same server as runHeadless() on a background thread,
// then connects a built-in client and exercises the protocol (get-config, set-working-dir, an
// unknown-command error path, ...), printing "[headless-test] ..." progress and a final
// ALL CHECKS PASSED / SOME CHECKS FAILED verdict to stderr. Exists to answer "is the server even
// working" (e.g. after a "Connection refused" from a separate client) without needing to juggle
// two terminals. `args` is the full argument list starting with "--headless-test". Returns 0 if
// every check passed, non-zero otherwise.
int runHeadlessSelfTest(const QStringList& args);
