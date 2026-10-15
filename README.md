# QRSSTVAE C++ port: working records (data only, never merge)

This orphan branch keeps the Build thread's working files from the C++ port of QRSSTVAE (2026-10-12 to 2026-10-15), which otherwise lived only in one cloud session's scratch space. It holds no code for the app; do not merge it.

The finished write-up is the retrospective Claude Doc, https://claude.ai/artifact/3AM3i4rVnF8WsyZQpBKZNM. The code is on waveform_l (stages 0 to 4 end at b84c1f8 and 4e92d36; the receive store fix is 5e1ae13 and 3787d20; GCC 16 / Qt 6.12 warning fixes are 17e68da).

- open-python-bugs.md: the open Python bug records, provisional (not re-checked against the code). The retrospective's deduplicated count is 89.
- records/: the 808 records porters and reviewers kept during the port, by kind (process, python-bug, review-bug, performance, numerics, deviation, open-item, simplification), plus waves.json and notes.json.
- retro/: the retrospective's Markdown source (retro.md, assembled from parts/), the facts sheet and the editing handoff.
- waves/: raw results and insights from waves 3 and 6b to 10, the port map and the stage 5 gather. These are the wave 7 to 10 insights that never reached /mnt/project-files/qrss-build/port-insights.md, because that folder returned I/O errors in this session.
- measure/: the scripts and results behind the retrospective's Measured section (bench.py, summarize.py, mtime.c, loc.py, envsize.py, results.md, results.jsonl). The plan's P8 suggests moving these into the repository.
- ideas-2026-10-12/: a copy of the 2026-10-12 optimization ideas document and its verification scripts.
- plan/: the approved port plan (port_plan.md) and the version before stage 4 was added.
- toolchain/hamlet-toolchain.sh: rebuilds Hamlet's GCC 16.2 and Qt 6.12 from conda-forge in a cloud container, to see the warnings Hamlet's build shows.
