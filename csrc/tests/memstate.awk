@load "filefuncs"
@load "time"
# The real memory state behind memslot.sh's admission, and its sampler.
#   gawk -f memstate.awk -v M=/tmp/netherite-mem -v CG=CGROUPDIR -v mode=snap
#     writes M/snap (atomically): "T COMMITTED LIMIT" in ms and MB, then one
#     "PID MB" line per live reservation (held.PID): what it may still add
#     to COMMITTED (allow(), below).
#   gawk -f memstate.awk -v M=... -v CG=... -v mode=sample -v out=FILE [-v every=1]
#     appends one row per sample to FILE and one row per finished job to
#     FILE.jobs (csrc/tests/memsample.sh).
# COMMITTED is what CG cannot give back without an OOM kill: memory.current
# less the file LRUs and reclaimable slab (the page cache fills the rest of
# the slice all day: 33 of 58 GB at 2:14 am 2026-09-28, memory.events max
# 990,880, oom_kill 0). shmem (the /tmp RAM disk, Xvfb's) stays in it.
function mb(b) { return int(b / 1048576) }
function rd(f,   v) { v = ""; if ((getline v < f) <= 0) v = ""; close(f); return v }
function now_ms() { return int(gettimeofday() * 1000) }
function anon(p,   f, l, a, s) {     # RssAnon + RssShmem KB (the sampler's)
  f = "/proc/" p "/status"; s = 0
  while ((getline l < f) > 0) if (l ~ /^Rss(Anon|Shmem):/) { split(l, a, " "); s += a[2] }
  close(f); return s
}
function tree(p,   s, i, c) {        # resident KB of p and its descendants
  s = rss[p] + 0                      # but another reservation's (a suite
  if (mode == "sample") tan += anon(p)  # job's oracle JVM holds its own)
  for (i = 1; i <= nkid[p]; ++i) {
    c = kid[p, i]; if (!(c in isheld)) s += tree(c)
  }
  return s
}
function measure(   f, l, a, n, i, c, p, k, cmd, name) {
  delete st
  f = CG "/memory.stat"
  while ((getline l < f) > 0) { split(l, a, " "); st[a[1]] = a[2] + 0 }
  close(f)
  cur = rd(CG "/memory.current") + 0
  lim = rd(CG "/memory.max"); hi = rd(CG "/memory.high")
  if (lim == "max" || lim == "") lim = 0; else lim = mb(lim)
  if (hi != "max" && hi != "" && (lim == 0 || mb(hi) < lim)) lim = mb(hi)
  committed = mb(cur - st["active_file"] - st["inactive_file"] - st["slab_reclaimable"])
  delete rss; delete nkid; delete kid
  cmd = "ps -e -o pid=,ppid=,rss="
  while ((cmd | getline l) > 0) {
    n = split(l, a, " "); if (n < 3) continue
    rss[a[1]] = a[3] + 0; k = ++nkid[a[2]]; kid[a[2], k] = a[1]
  }
  close(cmd)
  # held.PID and held.oracle.PID hold a bare MB count (every checkout's
  # memslot sums them); want.T.PID is a job at the head of the fair queue
  delete job; delete isheld; delete since; nj = 0; nwant = 0
  cmd = "ls -1 " M
  while ((cmd | getline l) > 0) {
    if (l ~ /^want\./) { ++nwant; continue }
    if (l !~ /^held\./) continue
    p = l; sub(/.*\./, "", p)
    if (!(p in rss)) continue
    ++nj; job[nj] = p; isheld[p] = 1
    need[p] = rd(M "/" l) + 0
    orc[p] = (l ~ /^held\.oracle\./)
    since[p] = (stat(M "/" l, sb) == 0 ? sb["mtime"] : 0)  # admitted (s)
  }
  close(cmd)
  for (i = 1; i <= nj; ++i) {
    p = job[i]
    tan = 0; jrss[p] = int((tree(p) + 1023) / 1024); jan[p] = int((tan + 1023) / 1024)
    c = rd("/proc/" p "/cgroup"); sub(/^0::/, "", c)
    jin[p] = (CGREL != "" && index(c "/", CGREL "/") == 1)
  }
}
# What a live job may still add to COMMITTED: the rest of its reservation
# (need less the resident MB of its process tree) for RAMP s after it was
# admitted, then at most TAIL (below); a job outside CG adds its whole use. Kinds: pool (a warm oracle member, which
# grows with every run it takes), oracle (any other JVM), native.
function kind(p,   nm) {
  if (!orc[p]) return "native"
  if (!(p in knd)) { nm = cmdname(p); knd[p] = (nm ~ / pool-member$/ ? "pool" : "oracle") }
  return knd[p]
}
function allow(p, t,   n, r, g, k) {
  n = need[p]; r = jrss[p]
  if (!jin[p]) return n > r ? n : r
  g = n - r; if (g <= 0) return 0
  k = kind(p)
  if (t / 1000 - since[p] < RAMP[k]) return g
  return g < TAIL[k] ? g : TAIL[k]
}
function cmdname(p,   f, l, a, n, i, o, key, data, bin) {
  # memslot.sh [--t F] [--key K] DATA CMD ...: its kind and run name
  f = "/proc/" p "/cmdline"; o = RS; RS = "\0"; n = 0
  while ((getline l < f) > 0) a[++n] = l
  close(f); RS = o
  for (i = 1; i <= n && a[i] !~ /memslot\.sh$/; ++i) ;
  ++i; key = ""
  if (a[i] == "--t") i += 2
  if (a[i] == "--key") { key = a[i + 1]; i += 2 }
  data = a[i]; sub(/\/+$/, "", data); sub(/.*\//, "", data)
  bin = key; if (bin == "") { bin = a[i + 1]; sub(/.*\//, "", bin) }
  return (bin == "" ? "?" : bin) " " (data == "" ? "?" : data)
}
BEGIN {
  CGREL = CG; sub(/^\/sys\/fs\/cgroup/, "", CGREL)
  # Measured on the dev host 2026-09-28 (lane/memslot2, the sampler over an hour
  # of every lane's jobs and merge trials): a native check reaches its peak
  # in 0 to 3 s, an oracle run's growth after 20 s was at most 156 MB (79
  # runs), a pool member keeps growing while it runs jobs (up to 1 GB after
  # 30 s, 459 MB after 60 s). With these, real use never passed what the
  # rule projected: 777 MB under at worst, where allowing pool members no
  # tail passed it by 1,223 MB.
  RAMP["native"] = 10; TAIL["native"] = 0
  RAMP["oracle"] = 30; TAIL["oracle"] = 0
  RAMP["pool"] = 60; TAIL["pool"] = 400
  if (mode == "snap") {
    t = now_ms(); measure()
    tmp = M "/snap.tmp." PROCINFO["pid"]
    printf "%d %d %d\n", t, committed, lim > tmp
    for (i = 1; i <= nj; ++i) { p = job[i]; printf "%s %d\n", p, allow(p, t) > tmp }
    close(tmp)
    system("mv -f " tmp " " M "/snap")
    exit 0
  }
  if (every == "") every = 1
  jobs = out ".jobs"; rows = out ".rows"
  if ((getline l < out) <= 0)
    print "t_ms\tcommitted\tlimit\tcurrent\tanon\tfile_lru\tshmem\tdirty\tslab_rec\tkernel\tev_max\tev_oom_kill\tpsi_full10\tavail\tnjobs\tnoracle\tnwant\tsum_need\tsum_rss\tsum_extra\tsum_over\tnear_peak\tnonjob\tproj\told" > out
  close(out)
  if ((getline l < jobs) <= 0)
    print "t_end_ms\tpid\tbin\tname\tin\tneed\tmax_rss\tt_to_max_s\tdur_s\tover_s" > jobs
  close(jobs)
  if ((getline l < rows) <= 0)
    print "t_ms\tpid\tage_s\tneed\trss\tanon\tin\tkind\tallow" > rows
  close(rows)
  while (1) {
    t = now_ms(); measure()
    ev = CG "/memory.events"; evmax = 0; evoom = 0
    while ((getline l < ev) > 0) { split(l, a, " "); if (a[1] == "max") evmax = a[2]; if (a[1] == "oom_kill") evoom = a[2] }
    close(ev)
    pf = ""; f = CG "/memory.pressure"
    while ((getline l < f) > 0) if (l ~ /^full/) { split(l, a, "[ =]"); pf = a[3] }
    close(f)
    avail = 0; f = "/proc/meminfo"
    while ((getline l < f) > 0) if (l ~ /^MemAvailable:/) { split(l, a, " "); avail = int(a[2] / 1024) }
    close(f)
    sn = 0; sr = 0; sa = 0; se = 0; so = 0; no = 0; np = 0; sal = 0
    delete alive
    for (i = 1; i <= nj; ++i) {
      p = job[i]; alive[p] = 1; n = need[p]; r = jrss[p]
      sn += n; no += orc[p]
      if (jin[p]) { sr += r; sa += jan[p]; se += (n > r ? n - r : 0) }
      sal += allow(p, t)
      so += (r > n ? r - n : 0); np += (n > 0 && r >= 0.9 * n)
      if (!(p in first)) { first[p] = (since[p] ? since[p] * 1000 : t); nm[p] = cmdname(p); jn[p] = n; ji[p] = jin[p]; mx[p] = 0; tmx[p] = t }
      if (r > mx[p]) { mx[p] = r; tmx[p] = t }
      if (r > n) ovs[p] += every
      printf "%d\t%s\t%d\t%d\t%d\t%d\t%d\t%s\t%d\n", t, p, (t - first[p]) / 1000, n, r, jan[p], jin[p], kind(p), allow(p, t) >> rows
    }
    for (p in first) if (!(p in alive)) {
      split(nm[p], bn, " ")
      printf "%d\t%s\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\n", t, p, bn[1], bn[2], ji[p], jn[p], mx[p], (tmx[p] - first[p]) / 1000, (t - first[p]) / 1000, ovs[p] + 0 >> jobs
      delete knd[p]; delete first[p]; delete nm[p]; delete jn[p]; delete ji[p]; delete mx[p]; delete tmx[p]; delete ovs[p]
    }
    fl = st["active_file"] + st["inactive_file"]
    printf "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", t, committed, lim, mb(cur), mb(st["anon"]), mb(fl), mb(st["shmem"]), mb(st["file_dirty"]), mb(st["slab_reclaimable"]), mb(st["kernel"]), evmax, evoom, pf, avail, nj, no, nwant, sn, sr, se, so, np, committed - sa, committed + sal, sn >> out
    fflush(out); fflush(jobs); fflush(rows)
    system("sleep " every)
  }
}
