## The Python bugs the port found

Porting the reference line by line, and then reviewing each module against it, found 127 distinct bugs in the Python. The count leaves out stale comments, dead code, test-only defects and notes on how the detection thresholds are calibrated; the threshold edge cases that are counted are in B8. 38 are fixed: 29 in both implementations and 9 in the Python only. 89 are still open. The C++ reproduces 52 of them, most because a parity test or pin holds it to the reference; it already gets 29 right; and for 8 argument checks in internal functions it has no counterpart or its behaviour was not established. Below, "C++ too" means the C++ reproduces a bug and "C++ right" means it does not. Python files are in sstvae/qrss/; the qrss_*.py scripts are at the top level.

### Fixed

- **known_symbols took a short header**: one bit short gave 3,574 positions against 3,573 values. track.py:454-469; ce73aff, both.
- **Gate V fitted its coarse timing grid twice**, the second time for the runners-up; sharing it cut a Python FULL verify of a false candidate in noise from 74.5 s to 63.1 s for CE and from 63.1 s to 50.6 s for L (measured), bit-identically. track.py:504, ltrack.py:984; ce73aff, both.
- **One bad hint lost the slot**: a NaN or infinite hint raised inside A2 and took every candidate's acquisition with it. acquire.py:1582, :1601-1608, :1622; ce73aff, both.
- **_receive_pass_l accepted a CE frame** and died on a None attribute instead of refusing it. receiver.py:344-345; ce73aff, both.
- **--image was checked after the receive**: a bad path failed after up to about 35 s of FULL receive, losing the result, and formats Pillow cannot write passed. qrss_receive.py:75; ce73aff and 73c1d3d, both.
- **Slots before 1970 were not refused**: the Python died in the scrambler after acquiring and tracking, and the C++ wrapped q and printed a pass. sequences.py:184-190; ce73aff, with 2671a6b for the C++; both.
- **load_npz trusted the archive**: an fs of 0 or a 2-D fe failed deep in the code, naming neither file nor member. channel.py:918-948; ce73aff, both, then 73c1d3d (Python only) for that fix's refusal of byte-swapped complex64.
- **qrss_listen.py ended a bad --slot in a traceback.** qrss_listen.py:185-188; c0c830f, Python only.
- **Association's correlation check never ran on short frames**: its median over all 50,600 latents stays 0 until 25,300 are covered, so on TINY, SHORT and MEDIUM another picture's pass attached under a colliding header unchecked. associate.py:173-190; b3596d0, both.
- **prov_add clobbered a provisional cluster**, dropping its members, and a parity sequence had pinned the bug. store.py:660; b3596d0, both.
- **pass_zw with the pass cache off** returned the entry it had just evicted, a KeyError. store.py:328; b3596d0, Python only.
- **due(), which lists the passes due in a slot, kept half an accumulator** when one raised partway. store.py:456-492; b3596d0, both.
- **The merge log's "accepted" changed type**: a number normally, a boolean when nothing overlapped. store.py:628; b3596d0, both.
- **An empty HOME** put the store in /.local/share/qrsstvae. store.py:67-80; b3596d0, both.
- **The store's caches never looked at the files**, so a listener's next rebuild undid a decode --em run in another process. store.py:129-136; b3596d0, widened to inode and change time in 66c0abd; both.
- **Temporary names were per process, not per thread**: two threads could collide, and two pack_hour calls on one hour could rename a half-written FLAC into place. store.py:123, frontend.py:532; b3596d0, d6ea91e, 4982928 and 033e7a5; both.
- **Dotted callsigns shared a file**: "K1.AB" and "K1.CD" both became acc/K1.json. store.py:396-398; 66c0abd, both.
- **A malformed accumulator dropped every picture's hints**: a TypeError, and after that fix an OverflowError, escaped due(). store.py:456-492; 66c0abd and b7506d6, both.
- **A uid that is not a string** ended decode --list with AttributeError. store.py:301-306; 375a85c, both.
- **pack_hour's stat sat outside its try**: a raw hour that vanished mid-pack lost every later hour in that pass. frontend.py:581; d6ea91e, both.
- **A negative passband fs** made read() loop forever. frontend.py:521-522; d6ea91e, both.
- **_HOUR_RE was too broad**, matching non-ASCII digits and a trailing newline. frontend.py:459; d6ea91e, Python only.
- **_merge_ranges leaked numpy integers**, which json refuses; unreachable. frontend.py:808; d6ea91e, Python only.
- **write_capture with an infinite sample** raised OverflowError in the gain. frontend.py:720-737; d6ea91e, both.
- **The pack swap trusted mtime alone**: on FAT or exFAT a write during the roughly 1 s pack was deleted with the raw hour. frontend.py:581; d6ea91e, Python only (it gained the C++'s write counter).
- **The packed FLAC was not synced**: a power cut could leave an empty FLAC and no raw hour. frontend.py:603; d6ea91e in the Python, 2490e9f in the C++.
- **Crash leftovers stayed forever, and the first sweep for them could stop writes**: it raised out of every write on a temp-named directory or a file Windows held open. frontend.py:779-800; d6ea91e and 02500ac, both.
- **A short raw hour was handled two ways**: read raised while write silently grew the file. frontend.py:693; d6ea91e, both.
- **One corrupt dial-log line stopped the poll**: in the dial log, the app's record of dial changes, an infinite dial_hz raised OverflowError on every refresh until the file rotated. live.py:477-498; 098f743, both.
- **The dial log was read in the locale's encoding**: under the C locale a U+0085 lost two dial changes. live.py:487; 098f743, Python only.
- **restrict read its spans twice**, so spans given as a generator gave an all-False mask. live.py:558; 098f743, Python only.
- **The "signal(s) at" log line repeated** once per accepted pass instead of once per refresh. live.py:1372-1374; a12c3b8, both.
- **FeRing, the listener's ring of front-end samples, wrote behind its window**, overwriting newer samples. live.py:211-230; 098f743, both.
- **NaN reached state.json**, so the app's JSON reader rejected the file and lost every tile. live.py:668; 098f743, with a142bd3 in the app; both.
- **A wrong-shaped state.json stopped the listener starting** until someone removed it. live.py:867; 098f743 and a12c3b8, both.
- **One failed live refresh retired the slot**, losing its remaining tiles and its end-of-slot receive. live.py:1151-1173; 098f743, both.
- **Tile.from_state took None** in float fields that are not optional. live.py:619; 4982928, Python only.
- **qrss_listen.py aborted after Ctrl-C**, exiting with status -6 (SIGABRT), because StdinSource's thread held BufferedReader's lock. live.py:1721-1727; 033e7a5, Python only.

### Still open

The 89 open bugs, counted by the Correctness entry that would fix them:

| Entry | Open | C++ too | C++ right | Not established |
|---|---|---|---|---|
| B1, the listener's state | 4 | 4 | 0 | 0 |
| B2, gate V's runner-up | 1 | 1 | 0 | 0 |
| B3, the frequency path's spline | 4 | 2 | 2 | 0 |
| B4, damaged accumulators | 4 | 3 (1 inferred) | 1 | 0 |
| B5, the store across processes | 1 | 1 | 0 | 0 |
| B6, a crashed recorder's WAV | 2 | 2 | 0 | 0 |
| B7, 16-bit pictures | 1 | 0 | 1 | 0 |
| B8, acquisition and association edges | 6 | 6 (2 inferred) | 0 | 0 |
| B9, the pulse table and integer phase | 6 | 2 | 4 | 0 |
| B10, file and command-line slips | 10 | 6 | 4 | 0 |
| B11, non-finite numbers | 14 | 10 (1 inferred) | 4 | 0 |
| B12, the Python listener's threads | 8 | 0 | 8 | 0 |
| B13, frame, waveform and header checks | 9 | 4 | 2 | 3 |
| B14, degenerate sizes and ranges | 15 | 8 | 2 | 5 |
| B15, the timing fits | 4 | 3 | 1 | 0 |
| Total | 89 | 52 | 29 | 8 |

Each entry in Correctness lists its bugs and which the C++ reproduces.
