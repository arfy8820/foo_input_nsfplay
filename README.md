# NSFPlay input for foobar2000 0.2.3

0.2.3 adds labeled keyboard selectors for all 32 channels and eight devices in the playback inspector. The selected item's mute checkbox and mixer controls follow the selector in the tab order.

0.2.2 adds a labeled keyboard selector for preferences, including all three Chip FDS options.

0.2.1 fixes the immediate crash when opening preferences. Window creation now happens after the foobar2000 service wrapper is fully constructed and owned; initialization notifications are ignored. The same construction order is used for the docked inspector. A real-window regression test is included in the normal build checks.

Native NSF/NSFe decoding with all 177 inventoried settings, preferences, presets, device/channel mixers, 32-channel masks, keyboard/waveform and file-information displays, memory tools, sidecar metadata and ReplayGain.

Get the package for your player from [GitHub Releases](https://github.com/arfy8820/foo_input_nsfplay/releases/latest). Use `foo_input_nsfplay.fb2k-component` for 64-bit foobar2000 or `foo_input_nsfplay-x86.fb2k-component` for 32-bit foobar2000. Open **Preferences > Components > Install**, select the matching package, apply and restart.

- Saved settings: Preferences > Playback > Decoding > NSFPlay.
- Live controls: View > NSFPlay > Playback inspector, or the NSFPlay inspector Default UI element.
- Build/test/package: `build.ps1` (see [build prerequisites and checkout layout](USER_GUIDE.md#build-and-test)).
- Optional isolated host-service tests: `test-host.ps1`.

See [USER_GUIDE.md](USER_GUIDE.md) for usage, compatibility and defaults, and [VALIDATION.md](VALIDATION.md) for test results.
