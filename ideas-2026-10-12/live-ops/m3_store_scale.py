"""M3 (live-ops): what does associate() cost as the multi-pass store grows?

A synthetic store: N_ACC accumulators (one FULL member each, distinct random
pictures, header decoded) and N_PROV provisional clusters (one weak headerless
pass each, distinct pictures, header LLRs at -8 dB per coded bit), as a week of
listening could leave. Then timed, as `_finish_apply` runs them (under the
listener's lock):
  - associate() of a new weak headerless pass of yet another picture (rules 2-4),
  - associate() of a new header-decoded pass of a new picture (rule 1 + resolve),
and the primitives: Store.accumulator() load, header.decode.
Usage: m3_store_scale.py N_ACC N_PROV"""
import shutil, sys, tempfile, time
sys.path.insert(0, "/home/user/sstvae"); sys.path.insert(0, "/home/user/sstvae/tests")
import numpy as np
from qrss_fakes import make_pass_result
from qrss_helpers import Q_TEST
from sstvae.qrss import associate as asc, header
from sstvae.qrss.store import Store

N_ACC, N_PROV = int(sys.argv[1]), int(sys.argv[2])
root = tempfile.mkdtemp(dir="/tmp/claude-0/-home-user-sstvae/92006b74-04c2-5210-829f-64b8a831fdbf/scratchpad/opt/live-ops")
st = Store(root)
rng = np.random.default_rng(1)
n = 50600


def hdr(pid):
    return header.HeaderFields(callsign="K1ABC", grid="FN42", picture_id=pid, mode=0,
                               segment=0, codec_id=0x1234)


t0 = time.time()
for i in range(N_ACC):
    a = rng.standard_normal(n)
    p = make_pass_result(a, 1.0, seed=10_000 + i, f_hz=1000.0 + 7.0 * i, hdr=hdr(0x1000 + i),
                         decoded=True, hdr_es_n0_db=0.0)
    asc.associate(p, st)
for i in range(N_PROV):
    a = rng.standard_normal(n)
    p = make_pass_result(a, 0.05, seed=20_000 + i, f_hz=1000.0 + 3.0 * i, hdr=hdr(0x5000 + i),
                         decoded=False, hdr_es_n0_db=-8.0)
    asc.associate(p, st)
print(f"built {N_ACC} accumulators, {len(st.prov_clusters())} provisional clusters "
      f"in {time.time() - t0:.1f} s", flush=True)

key = st.open_keys()[0]
t = time.time(); [st.accumulator(key) for _ in range(20)]
print(f"Store.accumulator(): {1000 * (time.time() - t) / 20:.1f} ms each", flush=True)
llr = make_pass_result(rng.standard_normal(n), 0.05, seed=1, hdr=hdr(0x9999),
                       hdr_es_n0_db=-8.0).hdr_llr
t = time.time(); [header.decode(np.asarray(llr, np.float64)) for _ in range(20)]
print(f"header.decode(): {1000 * (time.time() - t) / 20:.1f} ms each", flush=True)

weak = make_pass_result(rng.standard_normal(n), 0.05, seed=30_001, f_hz=2500.0,
                        hdr=hdr(0x7777), decoded=False, hdr_es_n0_db=-8.0)
t = time.time(); r = asc.associate(weak, st)
print(f"associate(weak headerless pass): {time.time() - t:.2f} s -> {r[1]}", flush=True)
good = make_pass_result(rng.standard_normal(n), 1.0, seed=30_002, f_hz=2510.0,
                        hdr=hdr(0x8888), decoded=True, hdr_es_n0_db=0.0)
t = time.time(); r = asc.associate(good, st)
print(f"associate(header-decoded pass, then resolve_provisional): {time.time() - t:.2f} s "
      f"-> {r[1]}", flush=True)
t = time.time(); due = st.due(Q_TEST + 2)
print(f"Store.due(): {time.time() - t:.2f} s", flush=True)
shutil.rmtree(root)
