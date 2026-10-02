# Release license checkpoint — pinned JUCE 8.0.10

This source milestone is private development/testing, not a license-cleared public release. No paid service, purchase, new license agreement or open-source licensing choice has been made on the owner's behalf. The package contains project source and test-fixture source, not the JUCE/VST3 SDK or compiled plugin binaries.

## Exact SDK used

The build uses JUCE 8.0.10 and its bundled VST3 SDK. That bundled LICENSE.txt carries a 2024 Steinberg copyright and offers the proprietary Steinberg VST3 license or GPL version 3. Its proprietary terms require a Steinberg-signed license agreement before publishing software under that route, and restrict redistribution of the SDK without prior written agreement. Other SDK files may have their own embedded license notices.

Primary source: [the license in the exact JUCE 8.0.10 SDK tree](https://github.com/juce-framework/JUCE/blob/8.0.10/modules/juce_audio_processors/format_types/VST3_SDK/LICENSE.txt). The same file was inspected in the pinned local checkout used by this build.

The newer VST3 SDK 3.8 licensing model must not be applied retroactively to this older bundled SDK. See Steinberg's [official licensing FAQ](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Licensing.html). No JUCE or SDK version has been silently substituted.

## Before distribution

1. Confirm the appropriate JUCE license for the owner's intended closed/open-source product and distribution/business situation. Free eligibility must be verified rather than assumed
2. Resolve the exact bundled VST3 SDK license route. The owner must decide on and approve any required agreement or open-source commitments; this build does not provide that approval
3. If choosing a newer SDK/JUCE route instead, make it an explicit dependency change with compatibility, build and audio regression validation. Do not replace pinned files to infer license clearance
4. Review required notices, trademarks, packaging and every shipped dependency before distributing binaries or installers

The local Linux fixture and host tests establish engineering behavior only. They do not establish public-release licensing, Windows readiness or third-party-plugin compatibility.
