# Native ghost and illumination verification

The exact approved ghost reference is preserved unchanged in Assets/ghost_brand_reference.png. SHA-256: d1fc647cd8882feb34a2e320690fd608267d56a8ba00433d7a6602680b6df79d. Source approval: user-selected Ghost in the Machine faceplate, Library libfile_67cc0810625481918cd6a3ea30cfa1c9. The application draws only its lower-left ghost/signature viewport (10,702,244,196), excluding the source artwork's controls, graphs and stray neighbouring lettering. No substitute ghost was invented.

StudioBranding draws the badge in the upper-right native host corner, preserves the original lit crown and frame, and displays a bright amber HYBRID ELITE MAFIA title with cyan STUDIO SUITE subtitle at startup. HardwareLookAndFeel draws crisp illuminated cyan labels and button text with subtle halo; active/pressed buttons remain amber. Disabled controls remain visibly disabled. No meter or control state is driven by decorative lighting.

The final running Linux window was inspected at 1280x720 on 2026-10-01. The ghost face, cyan eyes and signature are visible; the full top crown/title/subtitle and real control labels are readable. The initial badge crop included a neighbouring letter; the final crop excludes it. The initial title background clipped the crown; the final native title boundary preserves the whole original crown.

StudioBrandingTests passed actual JUCE raster checks at 1x and 2x for embedded asset identity, source eyes, native ghost/signature pixels, amber brand/cyan subtitle and painting isolation. HardwareLookAndFeelTests passed again after the label update, retaining functional pointer/fader geometry, genuine off/on/disabled states and calibrated real-meter raster behavior.

This is source/native-Linux verification, not Windows/DPI hardware certification. The cloud application reports no audio device and its meters truthfully show no audio.
