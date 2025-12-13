#!/bin/bash
# Linux VM helper script for testing on Ubuntu
# Mounts the project and jank LLVM build for development

set -e

VM_NAME="ubuntu-test"
PROJECT_DIR="/Users/pfeodrippe/dev/something"
JANK_DIR="/Users/pfeodrippe/dev/jank"

# Default action
ACTION="${1:-shell}"

case "$ACTION" in
    start)
        echo "Starting Linux VM with shared directories..."
        echo "  Project: $PROJECT_DIR -> /mnt/something"
        echo "  Jank:    $JANK_DIR -> /mnt/jank"

        # Stop if running
        tart stop "$VM_NAME" 2>/dev/null || true
        sleep 2

        # Start with shared directories
        tart run "$VM_NAME" --no-graphics \
            --dir="something:$PROJECT_DIR:tag=something" \
            --dir="jank:$JANK_DIR:tag=jank" &

        echo "Waiting for VM to boot..."
        sleep 10

        echo ""
        echo "VM started! Run './bin/linux-vm.sh mount' to mount shared directories inside VM"
        ;;

    mount)
        echo "Mounting shared directories inside VM..."
        tart exec "$VM_NAME" bash -c '
            sudo mkdir -p /mnt/something /mnt/jank
            sudo mount -t virtiofs something /mnt/something 2>/dev/null || echo "something already mounted or not available"
            sudo mount -t virtiofs jank /mnt/jank 2>/dev/null || echo "jank already mounted or not available"
            echo "Mounts:"
            mount | grep virtiofs || echo "No virtiofs mounts found"
        '
        ;;

    shell)
        echo "Opening shell in Linux VM..."
        echo "Project: /mnt/something (if mounted)"
        echo "Jank:    /mnt/jank (if mounted)"
        echo "LLVM:    ~/jank/compiler+runtime/build/llvm-install (VM local)"
        echo ""
        tart exec "$VM_NAME" bash
        ;;

    exec)
        shift
        tart exec "$VM_NAME" bash -c "$*"
        ;;

    status)
        echo "VM Status:"
        tart list | grep "$VM_NAME"
        echo ""
        echo "Checking mounts inside VM..."
        tart exec "$VM_NAME" bash -c 'mount | grep virtiofs || echo "No virtiofs mounts"' 2>/dev/null || echo "VM not running"
        echo ""
        echo "LLVM build status:"
        tart exec "$VM_NAME" bash -c 'ls -la ~/jank/compiler+runtime/build/llvm-install/usr/local/bin/clang 2>/dev/null && echo "LLVM build exists!" || echo "LLVM build NOT found"' 2>/dev/null || echo "VM not running"
        ;;

    stop)
        echo "Stopping VM..."
        tart stop "$VM_NAME"
        ;;

    build-jank)
        echo "Building jank in VM (using VM's LLVM build)..."
        tart exec "$VM_NAME" bash -c '
            cd ~/jank/compiler+runtime
            rm -rf build/jank-build
            export CC=$HOME/jank/compiler+runtime/build/llvm-install/usr/local/bin/clang
            export CXX="$HOME/jank/compiler+runtime/build/llvm-install/usr/local/bin/clang++ --gcc-install-dir=/usr/lib/gcc/aarch64-linux-gnu/13"
            ./bin/configure -GNinja -DCMAKE_BUILD_TYPE=Release \
                -Dllvm_dir=$HOME/jank/compiler+runtime/build/llvm-install/usr/local
            ./bin/compile
        '
        ;;

    test)
        echo "Running tests in VM (using mounted project)..."
        tart exec "$VM_NAME" bash -c '
            cd /mnt/something || { echo "Project not mounted! Run: ./bin/linux-vm.sh mount"; exit 1; }
            export PATH="$HOME/jank/compiler+runtime/build:$PATH"
            make test
        '
        ;;

    *)
        echo "Usage: $0 {start|mount|shell|exec|status|stop|build-jank|test}"
        echo ""
        echo "Commands:"
        echo "  start      - Start VM with shared directories"
        echo "  mount      - Mount shared directories inside VM"
        echo "  shell      - Open interactive shell in VM"
        echo "  exec CMD   - Execute command in VM"
        echo "  status     - Show VM and mount status"
        echo "  stop       - Stop the VM"
        echo "  build-jank - Build jank in VM (uses VM's LLVM)"
        echo "  test       - Run project tests in VM"
        echo ""
        echo "Example workflow:"
        echo "  ./bin/linux-vm.sh start    # Start VM with mounts"
        echo "  ./bin/linux-vm.sh mount    # Mount directories"
        echo "  ./bin/linux-vm.sh shell    # Open shell"
        echo "  ./bin/linux-vm.sh test     # Run tests"
        ;;
esac
