# 0.2.4 patched engine and unified package

Built both architectures on 25 November 2026 with MSVC v145 and foobar2000 SDK 2026-10-01. The engine includes all five supplied patches on upstream `6af5406` and a follow-up to bound title and playlist formatting and guard OPLL warm-up allocation. Replaying the six patch files in a clean checkout reproduced the same engine tree.

- The engine smoke tests passed on x86 and x64, including upstream memory-load timing, per-instance load errors and playlist strings, and audible VRC7 playback.
- The native-window accessibility suites and isolated foobar2000 host suites passed on both architectures. They exercised Preferences, inspector navigation, decoding, seeking, cancellation, Unicode metadata and ReplayGain.
- The unified archive contains the 32-bit DLL at its root and the 64-bit DLL under `x64/`. Both entries match the binaries exercised in the isolated host tests.

Live NVDA and the interactive component installation dialog remain untested for this build.

## 0.2.3 Win32 validation

Built on 25 November 2026 with MSVC v145 and foobar2000 SDK 2026-10-01. The Win32 build uses the same decoder and UI source as x64.

- The x86 engine and native-window accessibility suites passed. Coverage includes NSF/NSFe playback, all 177 settings, presets and Unicode sidecars, the FDS preferences, and keyboard access to all 32 channels and eight devices in the inspector.
- The isolated 32-bit foobar2000 2.26.3 test passed on Windows 11. It verified decoder registration, NSF/NSFe subsongs, audio output and seeking, cancellation, Unicode metadata and ReplayGain, and registration of Preferences and the Default UI inspector.
- The x86 package contains one DLL at the archive root. Its PE architecture is x86, it exports `foobar2000_get_interface`, and it matches the separately built DLL.

This build uses v145 and targets Windows 10 or newer. A Windows 7 build and live NVDA testing of the x86 inspector are still outstanding.

## 0.2.3 x64 validation

The x64 Release component was built with v145 using the existing engine and SDK libraries. Both smoke-test suites passed. The native-window test checks the inspector's accessible selector labels and every item in the Windows accessibility tree, keyboard traversal across all 32 channels and eight devices, mute and unmute, channel and device gain, pan, master gain, channel activity, and the tab order on all four inspector tabs.

The updated inspector still needs a live NVDA check inside foobar2000.

## 0.2.2 validation record

The native-window regression test covers all 177 parameter selections, the accessible names of the preferences controls, all three FDS options in the Windows accessibility tree, keyboard selection and Tab order, and applying edits to each FDS option. It also checks that the inspector exposes populated channel and device lists and skips the list on its File information and Memory tabs.

The x64 Release component and both smoke-test executables were built with v145 using the existing engine and SDK libraries. Both smoke tests passed. The forced dependency rebuild stalled after compilation in this session, so the component was linked without debug symbols and packaged from the incremental build.

The tester confirmed that all Preferences parameters were accessible with NVDA using the selectors and search box inside foobar2000.

## 0.2.1 validation record

Built on 24 November 2026 using MSVC 14.52 / v145, x64 Release, and foobar2000 SDK 2026-10-01.

Passed:

- The new real-window preferences regression test reproduced the 0.2.0 pure virtual call before the fix (exit 71). After the fix it passed 12 create/edit/apply/reset/select-all/destroy cycles, reentrant host closure, and eight docked-inspector construction/destruction cycles (exit 0).
- Opening preferences leaves settings unchanged and does not send user-edit callbacks. Invalid values cannot be applied. Controls are created only after the owning service pointer and reference-count implementation are ready.

- Component and standalone test executable compile/link.
- 177 schema keys and defaults match the earlier source inventory, with zero mismatches.
- Parameter bounds, unsigned high mask bits, complete settings serialization, legacy preset import and obsolete-key reporting.
- NSF/NSFe parsing, subsongs, labels, embedded timing, nonzero PCM and finite EOF.
- Mono/rate configuration, interactive infinite playback versus finite background decoding, speed-adjusted duration/EOF.
- Forward/backward/end seeking, concurrent instances, abort checks and malformed-header rejection.
- Chip telemetry and note mapping, queued mask/gain changes and queued memory writes.
- Unicode sidecar paths, escaped titles, multivalue/custom metadata encoding, millisecond timing, preservation of other songs and clearing sidecars.
- x64 PE architecture, component entry-point export, package contents and matching packaged DLL.

Not passed / outstanding:

- The isolated host-service test component did not reach initialization: the copied player stopped at startup. No `RESULT: PASS` was produced. This does not establish whether the startup issue is environmental or component-related.
- Computer-use access to foobar2000 was not approved in the earlier port session. The new standalone test exercises actual Windows controls, but does not replace visual, NVDA or theme integration testing inside the player.
- Automatic approval review blocked the baseline-player diagnostic command with `blocked by policy`, without a more specific reason.
- Real music across every expansion chip, long playback/conversion, keyboard-only and screen-reader operation, DPI transitions and theme changes require host QA.

The package is a functional x64 port candidate. Do not treat the standalone test results as proof of a fully validated foobar2000 release. The normal installed player/profile was not modified by the test setup.
