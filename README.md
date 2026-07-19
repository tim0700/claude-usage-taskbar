# claude-usage-taskbar

A [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) plugin that displays your Claude AI usage (5-hour session and 7-day weekly) directly in the Windows taskbar.

> [!NOTE]
> This is a maintained fork of [cubicj/claude-usage-taskbar](https://github.com/cubicj/claude-usage-taskbar) (archived July 2026). It relies on an unofficial usage endpoint that may change or rate-limit without notice — this fork mitigates that with `Retry-After`-aware backoff. Claude also shows usage officially in [Settings > Usage](https://claude.ai/settings/usage); this plugin simply puts it on the taskbar.

## What's new in this fork (v1.1.0)

- **Model-scoped weekly limit (e.g. Fable)**: the 7d item renders as two stacked bars — blue for the all-models weekly limit, purple for the model-scoped one — with the value text showing the higher (binding) percentage. Parsed from the newer `limits` array in the usage API, so future scoped limits are picked up automatically.
- **Smarter rate-limit handling**: honors the server's `Retry-After` on HTTP 429, shows a live countdown in the tooltip, and a manual click no longer extends the backoff window.
- **Thicker bars** and hardened parsing of server-supplied values.

| Taskbar | Hover |
|:---:|:---:|
| ![Taskbar](screenshots/taskbar.png) | ![Hover](screenshots/tooltip.png) |

## Prerequisites

- **Windows 10/11** (x64 or x86)
- **TrafficMonitor** v1.85+ ([download](https://github.com/zhongyang219/TrafficMonitor/releases))
- **Claude Code** installed and authenticated (`claude login`)

## Installation

1. Download the latest release zip for your architecture from [Releases](https://github.com/tim0700/claude-usage-taskbar/releases)
2. Extract `claude-usage-taskbar.dll` into TrafficMonitor's `plugins/` folder
   - To find this folder: **General Settings** → **Plug-in manage** → **Open plugin directory**
3. Restart TrafficMonitor
4. Right-click the taskbar window → **Taskbar Window Settings** → **Display settings...** → check **5h Usage** and/or **7d Usage**

## Configuration

Open settings via either:
- Right-click the taskbar window → **Plugin Options** → **Claude Usage Settings**
- **General Settings** → **Plug-in manage** → select the plugin → **Options**

| Setting | Default | Description |
|---------|---------|-------------|
| Credentials Path | *(auto-detect)* | Path to `.claude/.credentials.json`. Leave empty to use `%USERPROFILE%\.claude\.credentials.json` |
| Item Width | 160 | Display width in DPI-96 pixels (80–400) |
| Poll Interval | 60 | API poll interval in seconds (10–3600) |

Settings are stored in `claude-usage-taskbar.ini` next to the DLL.

## Usage

- Data refreshes automatically at the configured poll interval (default: 60s)
- **Click** the plugin item to force an immediate refresh — the display shows `...` while fetching
- Hover over the item for a tooltip with reset times and error details
- When your plan has a model-scoped weekly limit (e.g. Fable), the 7d item shows two bars: blue = all models, purple = the scoped model; the number is the higher of the two, and the tooltip lists each limit separately

## Troubleshooting

| Problem | Solution |
|---------|----------|
| "Credentials not found" | Install Claude Code and run `claude login` |
| "Authentication failed" | Run `claude login` to re-authenticate |
| No data shown | Click the plugin item to force refresh, or check TrafficMonitor logs |
| Plugin not visible | Ensure DLL architecture matches TrafficMonitor (x64 vs x86) |

## Building from Source

Requirements: Visual Studio 2022 Build Tools (MSVC v143), C++17

This repository can be edited from WSL, but builds are Windows-native. From WSL, call the Windows MSBuild executable directly:

```bash
MSBUILDFULL='/mnt/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/MSBuild/Current/Bin/MSBuild.exe'

# x64
"$MSBUILDFULL" claude-usage-taskbar.vcxproj -p:Configuration=Release -p:Platform=x64 -nologo -v:minimal

# x86
"$MSBUILDFULL" claude-usage-taskbar.vcxproj -p:Configuration=Release -p:Platform=Win32 -nologo -v:minimal
```

Output: `build/Release-x64/` or `build/Release-x86/`

## License

[MIT](LICENSE)
