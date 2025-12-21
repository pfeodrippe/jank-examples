# iOS Simulator Interaction

## MCP Tool
There's an MCP server for interacting with iOS simulator: https://github.com/joshuayoes/ios-simulator-mcp

This can be added to Claude Code's MCP configuration to enable:
- Taking screenshots of the simulator
- Launching/terminating apps
- Other simulator interactions

## Manual Commands

### Listing simulators
```bash
xcrun simctl list devices available | grep -i ipad
```

### Booting a simulator
```bash
xcrun simctl boot <SIMULATOR_ID>
```

### Installing an app
```bash
xcrun simctl install <SIMULATOR_ID> /path/to/App.app
```

### Launching an app with console output
```bash
xcrun simctl launch --console <SIMULATOR_ID> com.app.bundle.id
```

### Getting logs
```bash
xcrun simctl spawn <SIMULATOR_ID> log stream --predicate 'processImagePath contains "AppName"' --style compact
```

### Opening Simulator app
```bash
open -a Simulator
```

## Current iPad Simulator Used
- iPad Pro 13-inch (M4): 57653CE6-DF09-4724-8B28-7CB6BA90E0E3
