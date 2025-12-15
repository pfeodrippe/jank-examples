# Testing Linux Builds with Tart VMs

## Overview
Use tart VMs to test Linux standalone builds on macOS.

## Setup

### List available VMs
```bash
tart list
```

### Clone a fresh Ubuntu VM
```bash
tart clone ghcr.io/cirruslabs/ubuntu:latest ubuntu-sdf-test
```

### Start VM with shared directory
```bash
mkdir -p /tmp/vm-share
cp /path/to/file.tar.gz /tmp/vm-share/
tart run ubuntu-sdf-test --dir=share:/tmp/vm-share &
```

### Get VM IP
```bash
tart ip ubuntu-sdf-test
```

## Executing Commands in VM

The key discovery: use `tart exec` to run commands directly in the VM without SSH!

```bash
# Simple command
tart exec ubuntu-sdf-test uname -a

# Complex command with bash
tart exec ubuntu-sdf-test bash -c "cd ~ && ls -la"

# Install packages
tart exec ubuntu-sdf-test bash -c "sudo apt update && sudo apt install -y package-name"

# Download files
tart exec ubuntu-sdf-test bash -c "curl -LO 'https://example.com/file.tar.gz'"
```

**Note**: `tart exec` requires the Tart Guest Agent running in the VM. Cirrus Labs images (like `ghcr.io/cirruslabs/ubuntu:latest`) have it pre-installed.

## Network Issues
- SSH from host to VM may not work (no route to host)
- VM has internet access and can download from external URLs
- Use `tart exec` instead of SSH for running commands

## Workflow for Testing Linux Builds

1. Download artifact from CI:
```bash
gh run download <run-id> -n SDFViewer-Linux -D /tmp/linux-build
```

2. Clone and start VM:
```bash
tart clone ghcr.io/cirruslabs/ubuntu:latest ubuntu-test
tart run ubuntu-test &
sleep 20
```

3. Install gh CLI in VM and download:
```bash
tart exec ubuntu-test bash -c "curl -LO 'https://github.com/cli/cli/releases/download/v2.63.2/gh_2.63.2_linux_arm64.deb' && sudo dpkg -i gh_2.63.2_linux_arm64.deb"
```

4. Upload file to hosting service and download in VM, or use gh auth

5. Extract and run:
```bash
tart exec ubuntu-test bash -c "cd ~ && tar xzf SDFViewer-linux.tar.gz && cd SDFViewer-linux && ./SDFViewer"
```

## Cleanup
```bash
tart stop ubuntu-test
tart delete ubuntu-test
```
