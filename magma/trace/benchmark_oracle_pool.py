#!/usr/bin/env python3
"""Measure oracle matrix throughput and resource use at several pool sizes."""

import argparse
import json
import os
import pathlib
import statistics
import subprocess
import sys
import time

import run_oracle_matrix


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
RUNNER = HERE / "run_oracle_matrix.py"
DEFAULT_CASES = HERE / "oracle_pool_benchmark_cases.txt"
CLK_TCK = os.sysconf("SC_CLK_TCK")
PAGE_SIZE = os.sysconf("SC_PAGE_SIZE")


def read_cases(path):
    cases = []
    for raw in path.read_text().splitlines():
        value = raw.strip()
        if value and not value.startswith("#"):
            cases.append(value)
    if not cases:
        raise SystemExit(f"no benchmark cases in {path}")
    if len(cases) != len(set(cases)):
        raise SystemExit(f"duplicate benchmark case in {path}")
    return cases


def read_cpu():
    fields = pathlib.Path("/proc/stat").read_text().splitlines()[0].split()[1:]
    values = [int(value) for value in fields]
    return sum(values), values[3] + (values[4] if len(values) > 4 else 0)


def read_memory_gib():
    values = {}
    for line in pathlib.Path("/proc/meminfo").read_text().splitlines():
        key, value = line.split(":", 1)
        values[key] = int(value.split()[0])
    return (values["MemTotal"] - values["MemAvailable"]) / 1048576.0


def pool_pgids(instances):
    result = set()
    pool = HERE / "out" / "oracle_pool"
    for instance in range(instances):
        pid_file = pool / f"instance_{instance}" / "client.pgid"
        try:
            result.add(int(pid_file.read_text().strip()))
        except (OSError, ValueError):
            pass
    return result


def pool_process_totals(pgids):
    cpu_ticks = 0
    rss_bytes = 0
    processes = 0
    for stat_file in pathlib.Path("/proc").glob("[0-9]*/stat"):
        try:
            stat = stat_file.read_text()
            fields = stat[stat.rfind(")") + 2:].split()
            if int(fields[2]) not in pgids:
                continue
            cpu_ticks += int(fields[11]) + int(fields[12])
            rss_bytes += int(fields[21]) * PAGE_SIZE
            processes += 1
        except (OSError, ValueError, IndexError):
            continue
    return cpu_ticks, rss_bytes, processes


def percentile(values, quantile):
    ordered = sorted(values)
    index = min(len(ordered) - 1, int(len(ordered) * quantile))
    return ordered[index]


def write_report(out_dir, case_count, rows):
    payload = {"case_count": case_count, "results": rows}
    (out_dir / "benchmark.json").write_text(json.dumps(payload, indent=2) + "\n")
    baseline = rows[0]["cases_per_minute"]
    base_instances = rows[0]["instances"]
    lines = [
        "# Oracle pool scaling benchmark",
        "",
        f"Fixed workload: {case_count} cases",
        "",
        "| Clients | Pass | Elapsed | Cases/min | Speedup | Efficiency | Host CPU | Oracle CPU cores | Peak host RAM | Peak pool RSS | Mean case | P95 case |",
        "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in rows:
        speedup = row["cases_per_minute"] / baseline
        efficiency = speedup / (row["instances"] / base_instances)
        lines.append(
            f"| {row['instances']} | {row['passes']}/{case_count} | "
            f"{row['elapsed_seconds']:.2f}s | {row['cases_per_minute']:.2f} | "
            f"{speedup:.2f}x | {efficiency * 100:.1f}% | "
            f"{row['host_cpu_percent']:.1f}% | {row['oracle_cpu_cores']:.1f} | "
            f"{row['peak_host_used_gib']:.1f} GiB | {row['peak_pool_rss_gib']:.1f} GiB | "
            f"{row['mean_case_seconds']:.2f}s | {row['p95_case_seconds']:.2f}s |"
        )
    lines.append("")
    (out_dir / "benchmark.md").write_text("\n".join(lines))


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--instances", default="8,16,32")
    parser.add_argument("--port-base", type=int, default=25600)
    parser.add_argument("--case-file", type=pathlib.Path, default=DEFAULT_CASES)
    parser.add_argument("--out", type=pathlib.Path,
                        default=HERE / "out" / "oracle_pool_scale")
    parser.add_argument("--sample-seconds", type=float, default=0.5)
    parser.add_argument("--warn-host-memory-gib", type=float, default=250.0)
    parser.add_argument("--hard-host-memory-gib", type=float, default=350.0)
    parser.add_argument("--resume", action="store_true",
                        help="append missing levels to an existing benchmark.json")
    return parser.parse_args()


def main():
    args = parse_args()
    levels = [int(value) for value in args.instances.split(",")]
    if not levels or any(value < 1 for value in levels):
        raise SystemExit("--instances must contain positive integers")
    if levels != sorted(set(levels)):
        raise SystemExit("--instances must be unique and increasing")
    if args.sample_seconds <= 0:
        raise SystemExit("--sample-seconds must be positive")
    cases = read_cases(args.case_file)
    args.out.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("JAVA_HOME", "/usr/lib/jvm/java-8-openjdk-amd64")
    os.environ.setdefault("UV_CACHE_DIR", "/home/jawaugh/.cache/uv")
    os.environ.setdefault("TMPDIR", str(MAGMA.parent.parent / ".tmp"))
    pathlib.Path(os.environ["TMPDIR"]).mkdir(parents=True, exist_ok=True)

    report_json = args.out / "benchmark.json"
    rows = []
    if args.resume and report_json.exists():
        previous = json.loads(report_json.read_text())
        if previous.get("case_count") != len(cases):
            raise SystemExit("existing benchmark uses a different case count")
        rows = previous.get("results", [])
    completed_levels = {row["instances"] for row in rows}
    for instances in levels:
        if instances in completed_levels:
            print(f"keeping existing {instances}-client result", flush=True)
            continue
        used_before = read_memory_gib()
        if used_before >= args.hard_host_memory_gib:
            raise SystemExit(
                f"refusing {instances} clients: host uses {used_before:.1f} GiB "
                f"at the {args.hard_host_memory_gib:.1f} GiB hard ceiling")
        if used_before >= args.warn_host_memory_gib:
            print(f"warning: host memory already {used_before:.1f} GiB", flush=True)
        print(f"preparing {instances}-client pool ...", flush=True)
        run_oracle_matrix.start_pool(instances, "127.0.0.1", args.port_base)
        pgids = pool_pgids(instances)
        if len(pgids) != instances:
            raise SystemExit(
                f"found {len(pgids)} process groups for {instances} clients")

        run_out = args.out / f"n{instances}"
        command = [
            sys.executable, str(RUNNER),
            "--port-base", str(args.port_base),
            "--instances", str(instances),
            "--seeds", "0,1,2,3",
            "--out", str(run_out),
        ]
        for case in cases:
            command.extend(("--case", case))
        print(f"measuring {instances} clients on {len(cases)} cases ...", flush=True)
        host_start = read_cpu()
        pool_start = pool_process_totals(pgids)
        peak_host = read_memory_gib()
        peak_pool_rss = pool_start[1]
        wall_start = time.monotonic()
        process = subprocess.Popen(command, cwd=str(MAGMA), env=os.environ.copy())
        while process.poll() is None:
            time.sleep(args.sample_seconds)
            peak_host = max(peak_host, read_memory_gib())
            pool_now = pool_process_totals(pgids)
            peak_pool_rss = max(peak_pool_rss, pool_now[1])
            if peak_host >= args.hard_host_memory_gib:
                process.terminate()
                raise SystemExit(
                    f"host memory reached {peak_host:.1f} GiB hard ceiling")
        wall_seconds = time.monotonic() - wall_start
        if process.returncode not in (0, 4):
            raise SystemExit(
                f"{instances}-client benchmark failed with rc={process.returncode}")
        host_end = read_cpu()
        pool_end = pool_process_totals(pgids)
        summary = json.loads((run_out / "summary.json").read_text())
        elapsed = float(summary["elapsed_seconds"])
        case_seconds = [float(case["seconds"]) for case in summary["cases"]]
        passed = sum(case["status"] == "pass" for case in summary["cases"])
        host_total = host_end[0] - host_start[0]
        host_idle = host_end[1] - host_start[1]
        host_cpu = 100.0 * (host_total - host_idle) / host_total
        pool_cpu_seconds = (pool_end[0] - pool_start[0]) / CLK_TCK
        row = {
            "instances": instances,
            "passes": passed,
            "runner_returncode": process.returncode,
            "elapsed_seconds": elapsed,
            "wall_seconds": wall_seconds,
            "cases_per_minute": len(cases) * 60.0 / elapsed,
            "host_cpu_percent": host_cpu,
            "oracle_cpu_cores": pool_cpu_seconds / wall_seconds,
            "peak_host_used_gib": peak_host,
            "peak_pool_rss_gib": peak_pool_rss / (1024.0 ** 3),
            "pool_processes": pool_end[2],
            "mean_case_seconds": statistics.fmean(case_seconds),
            "p95_case_seconds": percentile(case_seconds, 0.95),
        }
        rows.append(row)
        write_report(args.out, len(cases), rows)
        print(
            f"{instances}: {row['cases_per_minute']:.2f} cases/min, "
            f"host CPU {host_cpu:.1f}%, oracle {row['oracle_cpu_cores']:.1f} cores, "
            f"peak host RAM {peak_host:.1f} GiB",
            flush=True,
        )
    print(f"report: {args.out / 'benchmark.md'}", flush=True)


if __name__ == "__main__":
    main()
