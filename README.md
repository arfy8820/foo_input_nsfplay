# NSFPlay input for foobar2000 0.2.4

0.2.4 incorporates fixes to NSFPlay’s shared scratch buffers, load errors, metadata formatting, VRC7 initialization, and timing for files loaded from memory. A unified installer is available from GitHub Releases for both 32-bit and 64-bit foobar2000.

0.2.3 adds labeled keyboard selectors for all 32 channels and eight devices in the playback inspector. The selected item's mute checkbox and mixer controls follow the selector in the tab order.

0.2.2 adds a labeled keyboard selector for preferences, including all three Chip FDS options.

0.2.1 fixes the immediate crash when opening preferences. Window creation now happens after the foobar2000 service wrapper is fully constructed and owned; initialization notifications are ignored. The same construction order is used for the docked inspector. A real-window regression test is included in the normal build checks.

Native NSF/NSFe decoding with all 177 inventoried settings, preferences, presets, device/channel mixers, 32-channel masks, keyboard/waveform and file-information displays, memory tools, sidecar metadata and ReplayGain.

Install `foo_input_nsfplay-unified.fb2k-component` through **Preferences > Components > Install**, apply and restart. The same file works with either player architecture. Download it from [GitHub Releases](https://github.com/arfy8820/foo_input_nsfplay/releases/latest).

- Saved settings: Preferences > Playback > Decoding > NSFPlay.
- Live controls: View > NSFPlay > Playback inspector, or the NSFPlay inspector Default UI element.
- Build/test/package: `build.ps1` (see [build prerequisites and checkout layout](USER_GUIDE.md#build-and-test)).
- Optional isolated host-service tests: `test-host.ps1`.

See [USER_GUIDE.md](USER_GUIDE.md) for usage, compatibility and defaults, and [VALIDATION.md](VALIDATION.md) for test results.

## License

The original foobar2000 wrapper code in this repository is available under the [MIT License](LICENSE).

The NSFPlay engine has its own [upstream reuse terms and notices](https://github.com/bbbradsmith/nsfplay/blob/6af5406e3325b5507bea1ae1a57c77d5efe5c7f3/readme.txt). The [engine patch series](patches/nsfplay/) preserves the patch authors' attribution. Release packages include the upstream NSFPlay notice and the separate foobar2000 SDK and PFC notices. See [Source and notices](USER_GUIDE.md#source-and-notices) for the engine revision and notice locations.
