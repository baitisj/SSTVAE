## How these were checked

**Sources.** The main source is the records kept during the port by every porter and reviewer, sorted into 808 items of eight kinds: 135 on process, 150 Python bugs, 144 review bugs, 76 on performance, 94 on numerics, 101 deviations, 74 open items and 34 simplifications (counted). The others are the approved port plan, the 2026-10-12 ideas document, whose 66 ideas were each given a port status, and one measurement run of the finished port on 2026-10-14 and 15 (Measured).

**Rechecking.** A record's status could be stale, since later waves fixed items that earlier ones left open, so every status, count and file:line in these parts was rechecked against the code at 4e92d36. 5e1ae13 and 3787d20 changed receive.cpp, the store and the passband store after that, so those three were read at 3787d20 and are cited by function name, which survives line shifts. The Python bug count was deduplicated: 150 recorded findings, plus 4 bugs found while fixing the Python, came to 127 distinct bugs (counted).

**Measurement method.** See "How it was measured" at the top of Measured.

**Not done.**
- No test suite was run for this document, and nothing in the repository was edited. The results at 4e92d36 in Where the port stands are the port's own final run.
- The measurement scripts (mtime.c, bench.py, summarize.py and the comparison scripts) are in the session's scratch directory, not in the repository (P8).
- The port plan and the 2026-10-12 document were read from local copies, since the shared folder returned I/O errors (T5).
- No inferred gain was tried, and the line counts given for fixes are estimates (inferred).
- The per-module timings in the records were not re-run.
- A claim marked "read from the code", such as B5's lost merge, was not exercised.
