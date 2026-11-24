# NSFPlay input for foobar2000 - x64 0.2.3

0.2.3 adds labeled keyboard selectors for all 32 channels and eight devices in the playback inspector. The selected item's mute checkbox and mixer controls follow the selector in the tab order.

0.2.2 adds a labeled keyboard selector for preferences, including all three Chip FDS options.

0.2.1 fixes the immediate crash when opening preferences. Window creation now happens after the foobar2000 service wrapper is fully constructed and owned; initialization notifications are ignored. The same construction order is used for the docked inspector. A real-window regression test is included in the normal build checks.

Native NSF/NSFe decoding with all 177 inventoried settings, preferences, presets, device/channel mixers, 32-channel masks, keyboard/waveform and file-information displays, memory tools, sidecar metadata and ReplayGain.

Download `foo_input_nsfplay.fb2k-component` from [GitHub Releases](https://github.com/arfy8820/foo_input_nsfplay/releases/latest). In 64-bit foobar2000, open **Preferences > Components > Install**, select the downloaded package, apply and restart.

- Saved settings: Preferences > Playback > Decoding > NSFPlay.
- Live controls: View > NSFPlay > Playback inspector, or the NSFPlay inspector Default UI element.
- Build/test/package: `build.ps1` (see [build prerequisites and checkout layout](USER_GUIDE.md#build-and-test)).
- Optional isolated host-service tests: `test-host.ps1`.

See [USER_GUIDE.md](USER_GUIDE.md) for usage, compatibility and defaults, and [VALIDATION.md](VALIDATION.md) for test results.
