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

## iPad RoughAnimator Access (pymobiledevice3)

Use these commands to inspect and pull `com.weirdhat.roughanimator` files from iPad (USB or Wi-Fi).

### Prerequisites

```bash
# iPad must be unlocked and trusted by this Mac.
# For Wi-Fi only, enable Finder option: "Show this iPad when on Wi-Fi" once via USB.
pymobiledevice3 usbmux list
```

If multiple devices appear, set UDID first:

```bash
export IOS_UDID="<from pymobiledevice3 usbmux list>"
```

### List RoughAnimator projects

```bash
export RA_BUNDLE=com.weirdhat.roughanimator
pymobiledevice3 apps afc --documents --udid "$IOS_UDID" "$RA_BUNDLE" <<'EOF'
ls
exit
EOF
```

If `IOS_UDID` is not set, remove `--udid "$IOS_UDID"`.

### Inspect a project and verify frame timestamps

```bash
pymobiledevice3 apps afc --documents --udid "$IOS_UDID" "$RA_BUNDLE" <<'EOF'
cd "voiture.ra"
cat data.txt
stat 0003/0000.png
exit
EOF
```

### Pull the full project locally

```bash
mkdir -p /Users/pfeodrippe/dev/something/roughanimator_from_ipad
pymobiledevice3 apps afc --documents --udid "$IOS_UDID" "$RA_BUNDLE" <<'EOF'
pull "voiture.ra" "/Users/pfeodrippe/dev/something/roughanimator_from_ipad"
exit
EOF
```

### Pull a single file quickly

```bash
pymobiledevice3 apps pull --udid "$IOS_UDID" "$RA_BUNDLE" \
  "voiture.ra/data.txt" \
  "/tmp/voiture_data.txt"
```
