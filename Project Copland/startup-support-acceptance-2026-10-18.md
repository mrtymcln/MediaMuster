# Local startup and support check — 18 October 2026

The local startup/support check passed without an application-code change.
The user chose to finish the non-scanner/parser cleanup and retain the current
logger rather than investigate logging overhead.

Source revision: `2d5772ea2488d809a482948e4a8ded515d09d2d3`. The current Debug
universal build uses Qt 6.5.3 and C++17; native execution was arm64 on macOS 15.8.2.
The build and bundle signature verification succeeded. Detailed results,
binary/source hashes and controlled-probe compile commands are in the
[receipt](evidence/startup-support-acceptance-2026-10-18.json).

## Normal application

The freshly built application ran its normal entry point and startup constructor.
Only its operation-history directory was redirected to disposable storage for
the initial launch. The existing diagnostic log was backed up before checking.

- The main window opened, discovered volumes and displayed startup messages in
  the Console without starting a media scan.
- All 331 pre-existing log lines eligible for retention survived. The five
  timestamped startup details and plain 60-character dividers matched the agreed
  layout; ordinary Console messages reached the saved log.
- Help → Reveal Diagnostics selected the actual `mediamuster.log` in Finder.
- All 17 original operation-history files remained byte-for-byte unchanged.

Reading accessibility state after Quit relaunched the app once. Both sequential
normal sessions are included in the retention comparison; neither ran media work.

## Disposable crash/recovery scenario

A separate native observer linked the current production application objects and
used `MainWindow::Normal`, rather than the UI-only startup mode. It used scratch
diagnostic and journal paths. The system-crash-directory accessor alone was
substituted with a scratch directory; the actual logger, cleanup, crash collector
and recovery implementations were retained. No `HOME` override was used.
The observer performed no UI actions; dialogs and menus were operated through CUA.

| Check | Result |
| --- | --- |
| Log retention | The 31-day-old fixture line disappeared; recent history survived both launches. |
| Crash-file retention | An expired collected copy disappeared; recent copies and an unrelated neighbour remained. Original report fixtures were untouched. |
| First launch | The crash notice appeared before recovery started. After its dismissal, Unfinished Business offered the pending disposable Copy job. |
| Second fresh process | The same job was still offered, without repeating the crash notice. |
| Dismiss recovery | Closing the prompt left the job pending. Source bytes were unchanged and the destination stayed empty. The journal was unchanged across the second dismissal. |
| Crash copy | Copied bytes and original modification time matched exactly. |
| Reveal Diagnostics | Finder selected the disposable log. |

Startup normally adds a journal reconciliation record before offering unfinished
work. An initial byte-equality expectation spanning that step was too strict;
the checks instead confirmed the job stayed planned with no attempts, and that
later dismissal did not change its journal. No Copy, Move, Resume or Stop was run.

## Tests and limits

The focused diagnostics and operation UI suites passed in 12.55 seconds:
6 and 94 QtTest passes respectively, including setup/cleanup, with no failures
or skips. Both native test applications were closed afterward.

This checks local functional wiring, not logging performance, human input latency,
a Release build, Windows or NEXIS. No recent genuine system crash report was
available. The controlled report was a filename/date fixture, not a generated
macOS crash or a power-loss test. Scratch sources, logs and intermediate receipts
remain in `/private/tmp/copland-startup-support-20261018/`; private existing log
contents were not copied into the repository.
