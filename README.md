# comgen

**comgen** (Command Generator) is a lightweight, natural language to Bash command generator for Linux and Windows. It uses the Anthropic API to convert English descriptions into executable shell commands, context-aware of your current environment (OS, shell, cwd).

## Features

- **Natural Language Input**: Type what you want to do (e.g., "find all large files in var"), and get the correct command.
- **Context Aware**: Knows your OS, Shell, Username, and Current Working Directory to generate accurate commands.
- **Interactive Session**: Works like a shell prompt.
- **Safety First**: Requires explicit confirmation (`y`/`n`) before executing any generated command.
- **Edit Mode**: Edit the generated command (`e`) in your preferred text editor (via `$EDITOR` or `$VISUAL`) before running it.
- **File Awareness**: Use `/ls` to make the AI aware of the files in your current directory.
- **Cross-Platform**: Native support for Linux (libcurl/libreadline) and Windows (WinHTTP).

## Prerequisites

You will need an **Anthropic API Key** to use this tool.

### Linux Requirements
- `gcc` & `make`
- `libcurl` (development headers)
- `libreadline` (development headers)

### Windows Requirements
- `MinGW` / `gcc` (if building from source)

## Installation

### Linux

1. Clone or download the source.
2. Run the installation script:
   ```bash
   ./install.sh
   ```
   
   Alternatively, build manually:
   ```bash
   make
   sudo mv comgen /usr/local/bin/
   ```

### Windows

1. Run `install.bat` as Administrator.
   - If `gcc` (MinGW) is in your PATH, it will compile the executable.
   - Validates that `comgen.exe` is created and copies it to `%SystemRoot%`.

## Configuration

## Configuration

On your first run, `comgen` will ask for your Anthropic API Key and save it automatically.

### Config File
The configuration is stored in a simple text format:
- **Linux**: `~/.config/comgen/config`
- **Windows**: `%APPDATA%\comgen\config`

You can edit this file manually:
```ini
api_key=sk-ant-12345...
model=claude-sonnet-4-20250514
```

### Environment Variables (Optional)
You can override the config file settings using environment variables:
- `ANTHROPIC_API_KEY`: Overrides the stored key.
- `COMGEN_MODEL`: Overrides the selected model.

## Usage

Start the tool by running:
```bash
comgen
```

### Example Session

```text
comgen> list all pdfs sorted by size
Thinking...
find . -name "*.pdf" -type f -exec ls -lh {} + | sort -k5 -rh

Execute? [y/n/e]: y
...
```

### Internal Commands
- `/ls`: Refreshes the internal file list context (sends current directory filenames to the AI). Use this if you change directories or want the AI to know about specific files.
- `/q`: Quit the session.
