# Linux Development and Build Workflow

This document describes the intended reproducible Linux workflow. MZM-Recomp is currently in **M0**, so a complete playable build command does not exist yet.

## 1. Current local workspace model

During M0, upstream experiments should be kept outside the tracked MZM-Recomp source tree or under an ignored local workspace.

Recommended local shape:

```text
~/proyectos/Recomp/Metroid-ZeroMissionRecomp/
├── Metroid - Zero Mission (USA).gba              # local only
├── Metroid - Zero Mission (Europe) (...).gba     # local only
└── _m0/                                          # local only
    ├── evidence/
    └── upstream/
        ├── mzm/
        ├── gbarecomp/
        └── agbcc/
```

The public Git repository is:

```text
https://github.com/HikariLucy/MZM-Recomp
```

ROMs must never be copied into the tracked repository.

## 2. Important shell safety rule

Do **not** paste this at the top of an interactive terminal session:

```bash
set -euo pipefail
```

Why:

- `set -e` tells the current shell to exit when an unhandled command fails;
- if pasted into an interactive shell, a later failing command can terminate the whole terminal shell;
- `set -u` can also abort on an unset variable.

For strict scripts, put those options inside a script and execute it as a child process:

```bash
bash ./scripts/some-check.sh
```

Then a failure exits the script, **not your parent terminal**.

For interactive debugging, prefer:

```bash
command_that_may_fail
echo "exit=$?"
```

or use explicit `if` checks.

## 3. M0 upstream pins

Initial baseline:

```text
metroidret/mzm
43b7fd52f552e4d38c1521ff9d4df5ee57e61493

mstan/gbarecomp
e7728148c6829ba526f682876430a0c9022dc6c0

jiangzhengwenjz/agbcc
59b966ed1b8f371856dcf99f1546c2fe89c678ca
```

Never replace these in old evidence with "latest." A deliberate pin upgrade should create new evidence.

## 4. Toolchain baseline

M0 expects at least:

- Git;
- Make;
- Python 3;
- C/C++;
- ARM binutils for the decomp build;
- CMake/Ninja or the build tools required by the pinned GBARecomp revision;
- SDL development dependencies required by the runtime.

Exact package names vary by Linux distribution and should be recorded once the host baseline is captured.

## 5. ROM verification

The primary target must match:

```text
SHA-1: 5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8
SHA-256: fc94f65380b65b870a30b9b04b39cca1dc63d6e46a4a373d3904adc0912ebc37
Size: 8388608 bytes
```

A future repository script should perform this check before generation/build work.

Unsupported ROMs must fail closed rather than proceed with guessed addresses.

## 6. M0.2 decomp reproducibility gate

The required result is:

```text
verified USA ROM
        │
        ▼
pinned metroidret/mzm + pinned agbcc
        │
        ▼
mzm_us.gba
        │
        ├── same SHA-1
        └── cmp byte-for-byte identical
```

M0.2 is not complete until both hash and byte comparison pass.

The ROM itself, the baserom copy and the rebuilt ROM remain local.

Safe evidence to retain publicly includes:

- commit SHAs;
- compiler/tool versions;
- expected and actual hashes;
- build exit status;
- byte-identical yes/no;
- sanitized logs that do not embed copyrighted binary material.

## 7. GBARecomp generation policy

When M1 begins:

1. verify the ROM before generation;
2. run the pinned analyzer/generator;
3. keep generated C++ local and ignored;
4. compile against the pinned runtime/integration;
5. never solve a generator defect by maintaining manual edits to generated C++;
6. record all missing dispatch/code-copy/resume findings as structured configuration or upstream issues.

## 8. Strict-static qualification

A native process that silently interprets unresolved code is useful during discovery, but is not enough for a strict-static milestone.

Milestone tests should explicitly distinguish:

- **hybrid discovery run**;
- **strict-static qualified run**.

The exact metrics/flags must follow the pinned GBARecomp revision and be documented with each milestone.

## 9. Evidence discipline

Every milestone evidence bundle should answer:

```text
What exact ROM?
What exact upstream commits?
What exact toolchain?
What command?
What route/test?
What passed?
What failed?
What fallback occurred?
Can another Linux machine reproduce it?
```

Do not substitute screenshots for machine-readable validation when better evidence is available.

## 10. Troubleshooting: terminal closes during commands

If a terminal suddenly closes after running the M0 command blocks, the first thing to check is whether `set -euo pipefail` was enabled in the interactive shell.

Open a new terminal and check:

```bash
echo "shell alive"
echo "$SHELL"
```

For future strict command blocks, run them from a temporary script:

```bash
cat > /tmp/mzm-step.sh <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

# commands go here
EOF

bash /tmp/mzm-step.sh
echo "script exit=$?"
```

Even if the script fails, the parent terminal should remain open and show its exit code.
