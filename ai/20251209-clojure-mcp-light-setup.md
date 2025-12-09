# Clojure MCP Light Setup

## Date: 2025-12-09

## What Was Done

Installed clojure-mcp-light globally for Clojure/ClojureScript development with Claude Code.

### Commands Executed

1. **Installed bbin via Homebrew:**
   ```bash
   brew install babashka/brew/bbin
   ```

2. **Installed clojure-mcp-light tools:**
   ```bash
   # Main hook for paren repair
   bbin install https://github.com/bhauman/clojure-mcp-light.git --tag v0.2.1

   # nREPL evaluation tool
   bbin install https://github.com/bhauman/clojure-mcp-light.git --tag v0.2.1 \
     --as clj-nrepl-eval \
     --main-opts '["-m" "clojure-mcp-light.nrepl-eval"]'

   # On-demand paren repair
   bbin install https://github.com/bhauman/clojure-mcp-light.git --tag v0.2.1 \
     --as clj-paren-repair \
     --main-opts '["-m" "clojure-mcp-light.paren-repair"]'
   ```

3. **Copied skill to global location:**
   ```bash
   git clone --depth 1 https://github.com/bhauman/clojure-mcp-light.git /tmp/clojure-mcp-light
   cp -r /tmp/clojure-mcp-light/skills/* ~/.claude/skills/
   ```

4. **Configured hooks in `~/.claude/settings.json`:**
   Added PreToolUse and PostToolUse hooks for Write|Edit operations to auto-repair Clojure delimiters.

### Installed Tools

| Tool | Location | Purpose |
|------|----------|---------|
| `clj-paren-repair-claude-hook` | `~/.local/bin/` | Hook for auto-repair of delimiters on file Write/Edit |
| `clj-nrepl-eval` | `~/.local/bin/` | Evaluate Clojure via nREPL |
| `clj-paren-repair` | `~/.local/bin/` | On-demand paren repair |

### Global Skill

- Location: `~/.claude/skills/clojure-eval/`
- Contains: `SKILL.md`, `examples.md`
- Provides Claude with Clojure evaluation capabilities

### Hooks Configuration

The following hooks were added to `~/.claude/settings.json`:

```json
"hooks": {
  "PreToolUse": [
    {
      "matcher": "Write|Edit",
      "hooks": [
        {
          "type": "command",
          "command": "clj-paren-repair-claude-hook --cljfmt"
        }
      ]
    }
  ],
  "PostToolUse": [
    {
      "matcher": "Edit|Write",
      "hooks": [
        {
          "type": "command",
          "command": "clj-paren-repair-claude-hook --cljfmt"
        }
      ]
    }
  ]
}
```

## What Was Learned

1. **bbin** is the Babashka package manager for installing bb scripts globally
2. **clojure-mcp-light** provides:
   - Automatic delimiter fixing for Clojure files
   - nREPL evaluation capabilities
   - Hooks that integrate with Claude Code
3. Skills are stored in `~/.claude/skills/` for global availability
4. Hooks can auto-fix Clojure parentheses/brackets on every file edit

## Usage

### nREPL Evaluation
```bash
# Discover nREPL ports
clj-nrepl-eval --discover-ports

# Evaluate code
clj-nrepl-eval -p <port> "(+ 1 2)"
```

### Manual Paren Repair
```bash
clj-paren-repair path/to/file.clj
```

### Skill Invocation
The `clojure-eval` skill should now appear in Claude Code's available skills.

## Requirements Met

- Babashka v1.12.209 (v1.12.212+ recommended for cljfmt)
- bbin v0.2.4
- `~/.local/bin` in PATH
