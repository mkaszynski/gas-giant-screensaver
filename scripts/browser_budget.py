#!/usr/bin/env python3
"""Fail-closed Linux cgroup envelope for sequential browser/rendering checks.

Local: python scripts/browser_budget.py --report captures/budget.json -- env ... node tests/web_memory_profile.cjs
CI may run this with sudo; the test itself retains the original user's identity.
"""
import argparse
import json
import os
from pathlib import Path
import pwd
import subprocess
import time
import uuid


def available_kib(text):
    return int(next(line.split()[1] for line in text.splitlines() if line.startswith("MemAvailable:")))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--memory-mib", type=int, default=1100)
    parser.add_argument("--memory-high-percent", type=int, default=82,
                        help="Soft reclaim threshold; 100 isolates hard-ceiling enforcement in CI")
    parser.add_argument("--cpu-percent", type=int, default=90)
    parser.add_argument("--seconds", type=int, default=1200)
    parser.add_argument("--reserve-mib", type=int, default=768)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command or min(args.memory_mib, args.cpu_percent, args.seconds, args.reserve_mib) <= 0:
        parser.error("A command and positive resource limits are required")
    if not 1 <= args.memory_high_percent <= 100:
        parser.error("The memory high percentage must be between 1 and 100")
    if available_kib(Path('/proc/meminfo').read_text()) < (args.memory_mib + args.reserve_mib) * 1024:
        parser.error("Insufficient available memory for the requested cap and reserve")
    unit = 'observatory-check-' + uuid.uuid4().hex[:10]
    mode = [] if os.geteuid() == 0 else ['--user']
    base = ['systemctl', *mode]
    identity = []
    if os.geteuid() == 0 and 'SUDO_UID' in os.environ:
        user = pwd.getpwuid(int(os.environ['SUDO_UID']))
        identity = ['--uid=' + str(user.pw_uid), '--setenv=HOME=' + user.pw_dir]
    launch = ['systemd-run', *mode, '--wait', '--pipe', '--remain-after-exit', '--unit=' + unit,
              '--working-directory=' + str(Path.cwd()), *identity,
              '-p', f'MemoryMax={args.memory_mib}M', '-p', f'MemoryHigh={max(1, int(args.memory_mib*args.memory_high_percent/100))}M',
              '-p', 'MemorySwapMax=0', '-p', f'CPUQuota={args.cpu_percent}%', '-p', 'TasksMax=192',
              '-p', f'RuntimeMaxSec={args.seconds}', '-p', 'OOMPolicy=kill', *command]
    report = {'unit': unit, 'memory_mib': args.memory_mib, 'memory_high_percent': args.memory_high_percent, 'cpu_percent': args.cpu_percent, 'samples': []}
    started = time.monotonic()
    process = subprocess.Popen(launch)
    try:
        while process.poll() is None:
            info = subprocess.run([*base, 'show', unit, '-p', 'MemoryCurrent', '-p', 'MemoryPeak',
                                   '-p', 'CPUUsageNSec', '-p', 'SubState', '-p', 'Result', '-p', 'ExecMainStatus'],
                                  capture_output=True, text=True).stdout
            available = available_kib(Path('/proc/meminfo').read_text())
            report['samples'].append({'seconds': round(time.monotonic()-started, 2), 'available_kib': available, 'cgroup': info})
            if available < args.reserve_mib*1024:
                report['stopped_reason'] = 'System memory reserve reached'
                subprocess.run([*base, 'stop', unit], capture_output=True)
                break
            if 'SubState=exited' in info or 'SubState=failed' in info:
                report['summary'] = info
                subprocess.run([*base, 'stop', unit], capture_output=True)
                break
            time.sleep(1)
        report['exit'] = process.wait(timeout=30)
    finally:
        if 'summary' not in report:
            report['summary'] = subprocess.run([*base, 'show', unit, '-p', 'MemoryPeak', '-p', 'CPUUsageNSec', '-p', 'Result', '-p', 'ExecMainStatus'], capture_output=True, text=True).stdout
        subprocess.run([*base, 'stop', unit], capture_output=True)
        subprocess.run([*base, 'reset-failed', unit], capture_output=True)
        new_parent = not args.report.parent.exists()
        args.report.parent.mkdir(parents=True, exist_ok=True)
        if os.geteuid() == 0 and 'SUDO_UID' in os.environ and new_parent:
            os.chown(args.report.parent, int(os.environ['SUDO_UID']), int(os.environ['SUDO_GID']))
        args.report.write_text(json.dumps(report, indent=2)+'\n')
        if os.geteuid() == 0 and 'SUDO_UID' in os.environ:
            os.chown(args.report, int(os.environ['SUDO_UID']), int(os.environ['SUDO_GID']))
    return report['exit'] or int('stopped_reason' in report)


if __name__ == '__main__':
    raise SystemExit(main())
