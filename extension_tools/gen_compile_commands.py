#!/usr/bin/env python3
"""
Generate compile_commands.json from `make -n` (dry-run) output.

This script runs `make -n` in the project root, parses printed compile
commands (those containing `-c` and a .c/.cpp source), and emits a
compile_commands.json suitable for clangd without executing any build
commands.

Usage:
  python3 tools/gen_compile_commands.py > compile_commands.json
"""
import os
import re
import shlex
import json
import subprocess
import sys


def main():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
    # run make -n in repo root to print commands without executing
    # run make -n but don't fail on non-zero return code; we still want the
    # printed commands from stdout so we can parse them.
    proc = subprocess.run(['make', '-n'], cwd=repo_root, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        print('make -n returned code', proc.returncode, file=sys.stderr)
        if proc.stderr:
            print('make stderr (truncated):', proc.stderr.splitlines()[-10:], file=sys.stderr)

    enter_re = re.compile(r"make(?:\[\d+\])?: Entering directory '(.+)'")

    entries = []
    cur_dir = repo_root
    for raw in proc.stdout.splitlines():
        line = raw.strip()
        if not line:
            continue
        m = enter_re.search(line)
        if m:
            cur_dir = os.path.normpath(m.group(1))
            continue

        # Heuristic: compiler invocation contains ' -c ' and a .c/.cpp file
        if ' -c ' not in line and ' -c\t' not in line:
            continue

        try:
            parts = shlex.split(line)
        except Exception:
            # fallback: split on spaces
            parts = line.split()

        # find source file token
        src = None
        for p in reversed(parts):
            if p.endswith('.c') or p.endswith('.cpp') or p.endswith('.cc'):
                src = p
                break
        if not src:
            continue

        # normalize paths
        if not os.path.isabs(src):
            src_path = os.path.normpath(os.path.join(cur_dir, src))
        else:
            src_path = src

        out_path = None
        if '-o' in parts:
            idx = parts.index('-o')
            if idx + 1 < len(parts):
                out = parts[idx + 1]
                if not os.path.isabs(out):
                    out_path = os.path.normpath(os.path.join(cur_dir, out))
                else:
                    out_path = out

        entry = {
            'directory': cur_dir,
            'file': src_path,
            'arguments': parts,
        }
        if out_path:
            entry['output'] = out_path

        entries.append(entry)

    json.dump(entries, sys.stdout, indent=2)


if __name__ == '__main__':
    main()
