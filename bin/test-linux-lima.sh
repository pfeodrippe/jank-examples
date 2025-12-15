#!/bin/bash
# Test Linux standalone app in Lima x86_64 VM
# Usage: ./bin/test-linux-lima.sh [--setup] [--download] [--run]
#   --setup    : Create and start Lima VM (first time only)
#   --download : Download latest artifact from CI
#   --run      : Run the app in VM
#   (no args)  : Do all steps

set -e
cd "$(dirname "$0")/.."

VM_NAME="linux-x64"
LIMA_CONFIG="/tmp/lima-x86_64.yaml"
TEST_DIR="/tmp/lima-test"
ARTIFACT_NAME="SDFViewer-Linux"

# Parse arguments
DO_SETUP=false
DO_DOWNLOAD=false
DO_RUN=false

if [ $# -eq 0 ]; then
    DO_SETUP=true
    DO_DOWNLOAD=true
    DO_RUN=true
else
    while [[ $# -gt 0 ]]; do
        case $1 in
            --setup) DO_SETUP=true; shift ;;
            --download) DO_DOWNLOAD=true; shift ;;
            --run) DO_RUN=true; shift ;;
            --help|-h)
                echo "Usage: $0 [--setup] [--download] [--run]"
                echo "  --setup    : Create and start Lima VM"
                echo "  --download : Download latest artifact from CI"
                echo "  --run      : Run the app in VM"
                echo "  (no args)  : Do all steps"
                exit 0
                ;;
            *) echo "Unknown option: $1"; exit 1 ;;
        esac
    done
fi

# Check Lima is installed
if ! command -v limactl &> /dev/null; then
    echo "Lima not installed. Install with: brew install lima lima-additional-guestagents"
    exit 1
fi

# Setup Lima VM
setup_vm() {
    echo "=== Setting up Lima x86_64 VM ==="

    # Check if VM exists
    if limactl list -q | grep -q "^${VM_NAME}$"; then
        # Check if running
        if limactl list --format json | jq -r ".[] | select(.name==\"$VM_NAME\") | .status" | grep -q "Running"; then
            echo "VM '$VM_NAME' is already running"
            return 0
        else
            echo "Starting existing VM '$VM_NAME'..."
            limactl start "$VM_NAME"
            return 0
        fi
    fi

    # Create config
    cat > "$LIMA_CONFIG" << 'EOF'
# x86_64 Ubuntu for testing Linux standalone binaries
vmType: qemu
arch: x86_64

images:
  - location: "https://cloud-images.ubuntu.com/releases/24.04/release/ubuntu-24.04-server-cloudimg-amd64.img"
    arch: "x86_64"

cpus: 2
memory: "4GiB"
disk: "20GiB"

mounts:
  - location: "~"
    writable: false
  - location: "/tmp/lima-test"
    writable: true

provision:
  - mode: system
    script: |
      #!/bin/bash
      set -eux
      apt-get update
      apt-get install -y gh xvfb libvulkan1 vulkan-tools \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev \
        cmake git build-essential
EOF

    echo "Creating Lima VM '$VM_NAME' (this takes a few minutes)..."
    mkdir -p /tmp/lima-test
    limactl start --name="$VM_NAME" "$LIMA_CONFIG" --tty=false

    # Build SDL3 in VM (not in Ubuntu 24.04 repos)
    echo "Building SDL3 in VM..."
    limactl shell "$VM_NAME" bash << 'SCRIPT'
set -e
if [ ! -f /usr/local/lib/libSDL3.so ]; then
    cd /tmp
    git clone --depth 1 --branch release-3.2.x https://github.com/libsdl-org/SDL.git SDL3
    cd SDL3
    mkdir build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc)
    sudo make install
    sudo ldconfig
fi
echo "SDL3 installed: $(ls /usr/local/lib/libSDL3*)"
SCRIPT

    echo "VM setup complete!"
}

# Download artifact
download_artifact() {
    echo "=== Downloading latest Linux artifact ==="

    mkdir -p "$TEST_DIR"
    cd "$TEST_DIR"
    rm -rf SDFViewer-linux* 2>/dev/null || true

    # Get repo from git remote
    REPO=$(git -C "$(dirname "$0")/.." remote get-url origin | sed 's/.*github.com[:/]\(.*\)\.git/\1/')

    echo "Downloading from repo: $REPO"
    gh run download --repo "$REPO" --name "$ARTIFACT_NAME"

    echo "Extracting..."
    tar xzf SDFViewer-linux.tar.gz

    echo "Downloaded to: $TEST_DIR/SDFViewer-linux"
    ls -la SDFViewer-linux/
}

# Fix and run the app
run_app() {
    echo "=== Running app in Lima VM ==="

    limactl shell "$VM_NAME" bash << 'SCRIPT'
set -e
cd /tmp/lima-test/SDFViewer-linux

echo "Checking binary..."
file bin/SDFViewer-bin

echo "Creating versioned symlinks..."
cd lib
ln -sf libLLVM.so libLLVM.so.22.0git 2>/dev/null || true
ln -sf libclang-cpp.so libclang-cpp.so.22.0git 2>/dev/null || true

# Copy system vulkan if not present
if [ ! -f libvulkan.so.1 ] || [ -L libvulkan.so.1 ]; then
    rm -f libvulkan.so.1 libvulkan.so
    cp /usr/lib/x86_64-linux-gnu/libvulkan.so.1 .
    ln -sf libvulkan.so.1 libvulkan.so
fi

# Copy SDL3 if not present
if [ ! -f libSDL3.so.0 ] || [ -L libSDL3.so.0 ]; then
    rm -f libSDL3.so.0 libSDL3.so
    cp /usr/local/lib/libSDL3.so.0 .
    ln -sf libSDL3.so.0 libSDL3.so
fi

cd ..

echo "Checking library dependencies..."
ldd bin/SDFViewer-bin 2>&1 | grep "not found" && echo "WARNING: Missing libraries!" || echo "All libraries found!"

echo ""
echo "Running app with xvfb (headless)..."
echo "Press Ctrl+C to stop"
echo ""

# Run with virtual framebuffer
xvfb-run -a ./SDFViewer 2>&1 | head -100
SCRIPT
}

# Stop VM
stop_vm() {
    echo "=== Stopping Lima VM ==="
    limactl stop "$VM_NAME"
}

# Shell into VM
shell_vm() {
    echo "=== Opening shell in Lima VM ==="
    limactl shell "$VM_NAME"
}

# Main
if [ "$DO_SETUP" = true ]; then
    setup_vm
fi

if [ "$DO_DOWNLOAD" = true ]; then
    download_artifact
fi

if [ "$DO_RUN" = true ]; then
    run_app
fi

echo ""
echo "=== Done ==="
echo ""
echo "Useful commands:"
echo "  limactl shell $VM_NAME              # Open shell in VM"
echo "  limactl stop $VM_NAME               # Stop VM"
echo "  limactl delete $VM_NAME             # Delete VM"
echo "  ./bin/test-linux-lima.sh --run      # Re-run app"
