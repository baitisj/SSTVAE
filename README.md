# QRSSTVAE streamed-copy data, 2026-10-11

Read-only snapshot, taken at about 04:51Z, of the receive-only QRSSTVAE
copy that streamed to YouTube from Hamlet. It is a second install,
separate from the station's main app.

- Audio: Utah WebSDR (websdr1.sdrutah.org:8901), 7066 kHz USB, taken
  from the page's decoded audio at 8000 Hz (scripts/stream/websdr_bridge.js).
- App and listener: qrss-live at fc183b5 (built in native/build-stream),
  with the listener running from that checkout's .venv.
- Times: the stream and the listener started at about 03:30:30Z, so the
  03:30Z slot misses its first ~45 s. ffmpeg was restarted at 03:43:34Z
  to extend the stream to 07:00Z; the listener was not touched.
- Rig control off, no schedule. This copy never transmitted.

## Files

- `store/live/state.json`: the listener's state, with the tiles and the
  last log lines it keeps.
- `store/live/tiles/*.png`: tile pictures. `<q>-<Hz>`, where q is the
  quarter-hour index (q*900 = slot epoch: 1990767 = 03:45Z,
  1990769 = 04:15Z, 1990771 = 04:45Z).
- `store/passes/*.npz`: one per pass: `<q>_<mHz>_<hash>`.
- `store/acc/*.{npz,json}`: the multi-pass accumulators, one per
  callsign and picture.
- `store/prov/`: provisional tiles (empty).
- `store/passband/`: front-end I/Q at 4000 Hz, int16, scale 8192
  (`meta.json`). `fe_<hour>` covers hour*3600 s.
  - `fe_497691.*`: 03:00Z, finished, packed as FLAC by the listener.
  - `fe_497692.*`: 04:00Z. **Partial**: a cp of the hour still being
    written, about 3065 s in. Raw .i16, 57.6 MB.
  - Each `.json` beside an hour lists the ranges written.
- `check_store/slot_HHMMZ.txt`: output of
  `scripts/qrss_check_store.py --store store --passband store/passband --state store/live --slot ...`
  run on this snapshot for slots 03:30 to 04:30Z. The 04:30Z slot was
  still in progress (its "hole" is just the future), and so is the
  04:00Z hour.
- `logs/`:
  - `sstvae.log`: the app's status log.
  - `config.json`: the copy's settings.
  - `app.log`: the app's stdout and stderr.
  - `bridge.log`: the WebSDR bridge's rate lines.
  - `ffmpeg.log`, `xvfb.log`, `run.log`, `extend.log`: the stream's own
    logs. The stream key is redacted, and the key file is not included.
  - The listener has no log file of its own: its stderr goes to the
    app's listener panel, and state.json keeps its last lines.
