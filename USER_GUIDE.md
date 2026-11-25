# NSFPlay 0.2.4

0.2.1 fixes the preferences-opening pure virtual call in 0.2.0. The page and docked inspector create their windows after their reference-counted service objects are fully constructed. Preferences initialization notifications cannot be mistaken for user edits.

## Install and open

The unified installer, `foo_input_nsfplay-unified.fb2k-component`, works with 32-bit and 64-bit foobar2000. In foobar2000, select **Preferences > Components > Install**, choose the package, apply and restart. Local builds also produce individual installers under `dist/`. Resolve competing NSF decoders in Preferences > Playback > Decoding if necessary.

Saved settings are under **Preferences > Playback > Decoding > NSFPlay**. The live interface is under **View > NSFPlay > Playback inspector**, or can be added as **NSFPlay inspector** in Default UI layout editing mode. Recent hosts also expose its automatic Playback Information command.

The x64 and Win32 builds use the same decoder, settings and inspector with foobar2000 2.0 or newer. MSVC v145 targets Windows 10 or newer. Windows 7 support would require a compatible toolset and runtime plus validation on Windows 7. The standalone inspector works independently of the active UI layout. No Winamp or MFC UI DLL is required.

## Preferences and presets

Choose a **Category**, optionally enter a search, and Tab to **Parameter**. Use the arrow keys to select a setting, then Tab to its editor. Alt+Down opens the full dropdown. For **Chip FDS**, the choices are **Low-pass cutoff (Hz)**, **Reset on $4085**, and **Write protect**. Boolean and enumerated settings have named values, and colors have a color picker. The help text explains invalid values. **Apply** saves your edits. **Reset** previews upstream defaults until you apply them.

All **177 constructor-defined settings** from the source audit are represented. Categories include Playback, Audio, Timing, Chips, Mixer, Channels, Display, Compatibility and Diagnostics. Sample precision remains fixed at 16 bits, as in NSFPlay; foobar2000 receives floating-point audio and controls hardware output precision.

- Initial defaults remain 48 kHz stereo, quality 10, default chip options, filters and gain. Fallback duration is 300 seconds plus a five-second fade; embedded NSFe timing and enabled sidecar overrides take precedence.
- Live-capable settings are identified in the help text. Applying them updates the playing decoder. Format, region, patch, song ordering and initialization settings take effect after reopening a track. Playback restart/rescan requirements are reported to the host.
- Fast seeking is used only when the host permits approximate seeking; otherwise the adapter renders and discards audio. Canonical test decoding disables randomized chip startup without changing saved preferences.
- `REGION` includes automatic, three preferences and three forced-region modes. `MULT_SPEED=256` is normal speed. The adapter anchors elapsed emulation time to accumulated samples, avoiding per-block millisecond drift at fractional sample rates.
- `MASK` is an unsigned 32-bit decimal mask. `MASK_INIT` defaults to enabled and clears the effective mask when interactive playback initializes. Disable it to use a saved startup mask.
- `PLAY_ADVANCE` supports advancing, infinite interactive playback, and stop after current track. Background conversion, testing and scanning always remain finite. The component only manages host stop-after-current when its single-track policy needs it.
- Device gain is 0-255; master gain also retains the Easy dialog's endpoint 256. Channel volume is 0-128 and pan is 0-255, centered at 128.

Save/load/delete complete named presets using the bottom row. Presets live under the profile's `nsfplay/presets` folder. Import/export uses INI files. Import accepts old `in_yansf.ini` settings and `PRESETxxxx` sections, reports obsolete/unknown keys, and imports named presets. Imported presets are saved immediately; imported main settings wait for Apply. Obsolete per-device FILTER/QUALITY keys are reported rather than misrepresented as active parameters.

Changing `NSFE_PLAYLIST` changes positional subsong mapping: reload/re-enumerate affected playlist and library entries. The 0.1.0 one-based subsong IDs are retained.

## Playback inspector

On **Channels**, Tab to **Sound channel** and use the arrow keys to choose any of the 32 slots, including the extra OPLL/VRC7 slots. Alt+Down opens the dropdown. Tab to **Mute selected channel** and press Space to toggle it. Continue with Tab to reach channel volume, pan, master volume and channel activity. Color and the mixer actions follow.

On **Device mixer**, use **Sound device** the same way. The eight devices are APU1, APU2, FDS, MMC5, 5B, VRC6, VRC7 and N163. Tab from the selector to the selected device's mute checkbox, device volume and master volume.

**Channel activity** reads out chip volume, frequency or register value, note, tone when available, and output for the selected channel. The read-only field holds its values while focused so you can read or copy them. The inspector also shows a colored keyboard, FDS/N163 wave tables, and reconstructed pulse, triangle and saw shapes. When a waveform is unavailable, the graph shows labeled output history. Graphical/hex modes, refresh rate, display delay and 5B envelope/noise colors are available in Preferences.

**File information:** labels/authors, embedded timing, region, CPU addresses, tick periods, banks, chip use, NSF2 flags, ripper and NSFe text. Information and legacy NSF playlists can be exported. The component registers NSF/NSFe, not the Winamp host's pseudo-path/playlist protocol; use normal foobar2000 subsongs and playlist saving for native playlists.

**Memory:** a 256-byte page, hexadecimal address/value fields, queued byte writes and a complete 64 KiB dump. Inspection avoids reads from side-effecting chip registers. Requests are processed by the decoder: resume playback if paused.

Inspector edits affect only the interactive session. **Save as default** persists its live-capable settings. Running conversions/scans keep their snapshots. Already-buffered audio delays audible changes. Timestamped chip snapshots are selected using host playback time plus `INFO_DELAY`, rather than showing the decoder ahead of the music. History covers twelve seconds; unusually large output buffers can exceed it.

## Metadata and compatibility

Normal foobar2000 **Properties** edits go into a sibling `.tag` file. Original NSF/NSFe bytes remain unchanged. Multivalue/custom metadata and ReplayGain use component-specific extensions within that sidecar. `NSF_PLAY_MS`, `NSF_LOOP_MS` and `NSF_FADE_MS` are editable per-song timings in milliseconds. Extension keys preserve millisecond precision while legacy song lines retain whole-second values for older players.

`READ_TAGINFO` enables legacy `file.tag`, then `taginfo.tag` reading. Component-owned sidecars are read automatically so Properties edits remain visible with this toggle off. UTF-8, UTF-16LE and Windows ANSI text are accepted. Explicit edits write sibling sidecars and do not rewrite shared global `taginfo.tag` files.

`WRITE_TAGINFO` opts into sidecar creation during interactive playback. Together with `UPDATE_PLAYLIST`, detected timings can be saved at EOF. Background decoders do not write sidecars or CPU logs. Detected duration updates use both legacy dynamic info and modern input-v5 live info. External sidecar edits may require reloading because the source music file's timestamp does not change.

The legacy title template is for compatibility output; normal playlist display uses host title formatting. Supported tokens include `%T`, `%A`, `%C`, `%L`, `%F`, `%P`, track/start/total tokens and `%%`, with bounded output. CPU logs use a safe internal title; relative `LOG_CPU_FILE` names resolve under the profile's `nsfplay` folder. Logging and reset-related chip options require reopening playback.

## Build and test

Install Visual Studio C++ Build Tools and a Windows SDK. From a workspace directory, clone the component and the pinned NSFPlay engine:

```powershell
git clone https://github.com/arfy8820/foo_input_nsfplay.git
New-Item -ItemType Directory -Force bbbradsmith | Out-Null
git clone https://github.com/bbbradsmith/nsfplay.git bbbradsmith/nsfplay
git -C bbbradsmith/nsfplay checkout --detach 6af5406e3325b5507bea1ae1a57c77d5efe5c7f3
$enginePatches = (Get-ChildItem foo_input_nsfplay/patches/nsfplay/*.patch | Sort-Object Name).FullName
git -C bbbradsmith/nsfplay am --keep-cr @enginePatches
```

Extract the [foobar2000 SDK 2026-10-01](https://www.foobar2000.org/SDK) into `SDK-2026-10-01` at the workspace root. Its `foobar2000` and `pfc` directories should sit directly under `SDK-2026-10-01`. The resulting layout is:

```text
workspace/
  foo_input_nsfplay/
  bbbradsmith/nsfplay/
  SDK-2026-10-01/
```

From `foo_input_nsfplay`, build and test:

```powershell
.\build.ps1
.\build.ps1 -PlatformToolset v145
.\build.ps1 -Platform Win32
.\package.ps1 -Platform Both
# Process-only override if local script execution is disabled:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

This builds x64 Release by default. `-Platform Win32` builds x86 Release and produces `dist/foo_input_nsfplay-x86.fb2k-component`. Either build runs engine/settings/sidecar smoke tests and generates original synthetic music under `tests/fixtures`. SDK references also create intermediates in the SDK tree. The script supports MSVC v143/v145. The component uses the Visual C++ runtime.

Run `package.ps1 -Platform Both` after building both architectures from the same source. It checks each DLL’s architecture and packages them as `dist/foo_input_nsfplay-unified.fb2k-component`.

The normal build also runs `preferences_smoke.exe` against the actual preferences/inspector implementation. It creates its own windows and an isolated test profile. Its SDK dependencies use `shared.dll` from a foobar2000 installation of the same architecture. Pass `-FoobarDirectory` to use a different directory. `-SkipPreferencesTests` explicitly permits building without that regression check.

Optional integration tests copy an installed x64 player into an isolated workspace folder:

```powershell
.\test-host.ps1 -FoobarDirectory 'C:\Program Files\foobar2000' -PlatformToolset v145
.\test-host.ps1 -Platform Win32 -FoobarDirectory 'C:\Program Files (x86)\foobar2000' -PlatformToolset v145
```

The test-only component verifies portable mode/profile location before changing configuration. It checks host registration, audio chunks, subsongs, EOF, seeking, cancellation, sidecars, custom/multivalue metadata and ReplayGain. Results go to `build/host-validation/fixtures/host-smoke.txt` for x64 or `build/host-validation-Win32/fixtures/host-smoke.txt` for x86.

Core smoke coverage includes 177 defaults, validation and INI round trips, high mask bits, mono and sample rate changes, loop policies, chip telemetry, queued controls, memory writes, concurrency, cancellation and Unicode sidecars. Synthetic fixtures exercise the base APU and VRC7.

The engine retains a cancellable component-wide lock around emulation because the patched core still uses shared facilities such as randomization. Noise and triangle randomization remain configurable and enabled by default. Input is capped at 16 MiB. Invalid chunk sizes, song indices and over-capacity banked programs are rejected.

Live NVDA, visual layout, theme changes and broader expansion-chip playback remain areas for in-player testing.

See `VALIDATION.md` for architecture-specific results and remaining interactive checks.

## Source and notices

`settings.cpp` defines the schema, `host.cpp` handles configuration and session selection, and `input.cpp` implements decoder and metadata services. `engine.cpp` adapts NSFPlay and its telemetry, `tags.cpp` handles sidecars, and `ui.cpp` provides Preferences and the inspector. The patch series is in `patches/nsfplay`.

NSFPlay by Brezza, maintained by Brad Smith: https://bbbradsmith.github.io/nsfplay/ . Baseline: `6af5406e3325b5507bea1ae1a57c77d5efe5c7f3`. Retain original source notices. Packaged notices cover the NSFPlay README and foobar2000 SDK/PFC; other third-party notices remain in upstream headers.
