# AGENTS.md

## Jank Setup (Required)

This repo expects jank at:

`/Users/pfeodrippe/dev/jank/compiler+runtime`

Executable path:

`/Users/pfeodrippe/dev/jank/compiler+runtime/build/jank`

## One-Time Shell Setup (zsh)

Run exactly:

```bash
echo 'export JANK_SRC=/Users/pfeodrippe/dev/jank/compiler+runtime' >> ~/.zshrc
echo 'export PATH="$JANK_SRC/build:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

## Per-Session Setup (if PATH is missing)

Run exactly:

```bash
export JANK_SRC=/Users/pfeodrippe/dev/jank/compiler+runtime
export PATH="$JANK_SRC/build:$PATH"
```

## Verify Setup

Run exactly:

```bash
test -x /Users/pfeodrippe/dev/jank/compiler+runtime/build/jank
command -v jank
jank --help
```

If `command -v jank` is empty, use absolute path directly:

```bash
/Users/pfeodrippe/dev/jank/compiler+runtime/build/jank --help
```

## Repo Build/Run Quick Commands

```bash
make build-sdf-deps
make fiction
make fiction-wasm
```

## Agent Rule

Before running jank-based validation, always run:

```bash
export JANK_SRC=/Users/pfeodrippe/dev/jank/compiler+runtime
export PATH="$JANK_SRC/build:$PATH"
```
