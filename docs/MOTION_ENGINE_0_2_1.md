# Motion Engine 0.2.1: timing, interaction and editor reliability

## Compatibility and routing

This update retains the ten motion models, shared MotionPaths geometry, physical
Orbit/Lissajous HIT and throw deviations, stable CLAP parameter/port identities,
and NCLP v1 parameter state format. The null-clap framework remains pinned to
`9de3106360f324ee4b08906595c54a3e138f6cb7`.

The safe-routing branch remains the basis of this release. Motion 1-8 are bipolar
`-1..+1` auxiliary control signals for Bitwig's Audio Rate modulator. Leave Rectify
off for normal bipolar operation. The optional v0.3.0 controller extension is
only a diagnostic companion, not a target mapper. Remove or replace an older
extension and restart Bitwig to remove its exposed target aliases. No new direct
controller-mapping mechanism has been introduced.

## Timing fix

The old plugin analysed audio once per host/event span, advanced the simulation,
and interpolated between the beginning/end values across that same span. Host
buffer size and even redundant automation events could therefore change the
modulation. A regression test reproduced a maximum bipolar output difference of
0.604116678 at 44.1 kHz when changing only the buffer size from 1 to 17 samples.

Analysis and output updates now follow a persistent 240 Hz clock across host
blocks and event boundaries. Completed analysis windows drive the existing
physics; sample-rate CV interpolation uses only previously computed states.
The 44.1 kHz clock handles fractional samples per tick instead of rounding to a
permanent integer interval. Audio analysis coefficients also scale with elapsed
time rather than the number of callback invocations.

This is **not** a sample-rate physics solver. Hits are timestamped on reception
and consumed at the next simulation tick, at most about 4.17 ms later. Causal CV
interpolation adds approximately one control interval of response delay; output
smoothing adds its configured response. Several HIT requests in one tick may
coalesce because HIT is a global body impulse, not a polyphonic voice system.
The main stereo input remains an undelayed passthrough. Audio-reactive behavior
can sound different from older builds because its timing is now fixed rather
than dependent on the host buffer size.

## Input, state and thread boundaries

- Native CLAP note-ons now trigger HIT alongside raw MIDI notes. A native
  velocity-zero note-on remains an onset; raw MIDI velocity zero is a release.
  Invalid ports, addresses and non-finite native velocities are rejected.
- MIDI note-hit activity is visible in the footer, independent of the bridge.
- State restore rejects unknown opaque payloads and requests an audio-owned
  reset instead of writing core parameters from the main thread. The first hit
  after restore/reset is preserved.
- GUI changes retain the newest unsent value and gesture end when the parameter
  event queue is congested. Text, wheel and default-value changes use complete
  gestures, as do canvas Zone edits.
- A bounded, coherent drag mailbox publishes final position and release velocity
  together. A complete drag/release between ticks no longer loses its placement.
  The audio reader makes at most three attempts and never waits on the GUI.
- Parameter and analysis inputs are sanitized against NaN/infinity. Analysis
  sanitation does not silently alter the main stereo passthrough.
- Reset now reseeds the deterministic random generator for repeatable resets of
  all models. It preserves model, Zone, range, smoothing and routing settings.
- Output constant masks are explicitly cleared; float/double and in-place main
  audio paths have direct regression coverage.

## Editor

- Existing embedded selectors remain in use, including the keyboard popup path.
  No return to the stock desktop-popup behavior that caused lost clicks.
- Value refresh leaves active text editing and mouse drags alone. Changing the
  selected Zone commits its old value editor before switching parameter IDs.
- Output Minimum/Maximum fields use real percentages: `50` and `50%` mean 50%.
  Invalid text preserves the previous value. Double-click restores defaults.
- Output strips show their auxiliary lane and actual bipolar CV value. **FLIP**
  swaps the output endpoints; inverted ranges are explicitly marked.
- The disabled pseudo-mapping controls and obsolete missing-mapping warnings are
  removed. Audio Rate is the actual routing path; the bridge is optional.
- Closing/hiding the editor or losing focus finishes its owned canvas drag and
  balances Zone gestures. Escape cancels a canvas gesture without a throw.
- A stationary hold after a flick no longer releases stale throw velocity.
- HIT can be triggered by H/Enter outside value editing; RESET clears the trail
  and restarts motion without changing the configuration.
- Trails are stored in world coordinates so resizing cannot leave a displaced
  screen-space trail. Route and constraint guides use effective parameter values
  when the host modulates model controls.

## Automated validation

The headless contract executable compiles the actual MotionEnginePlugin and
MotionEngineCore. Only GUI-delegate creation and bridge-worker startup are
excluded from that target. It verifies buffer sizes 1/17/64/511/2048 at
44.1/48/96 kHz, redundant event splits, no pre-hit output changes, raw/native
notes, numeric rejection, quick and concurrent gestures, queue backpressure,
state rejection/restore, deterministic reset and audio passthrough.

Full builds also compile the JUCE editor test. It renders 1120x700, 1320x820 and
1700x1000 layouts, checks control bounds, exercises percentage text entry during
refresh, Zone-selection ownership and model selectors, and closes an actual
canvas-owned mouse gesture without delivering mouse-up.

Windows and Linux builds run the core, plugin, editor and all five framework
suites. Separate Debug and optimized AddressSanitizer/UndefinedBehaviorSanitizer
jobs run the seven headless suites. Release assertions remain enabled in tests.
The package includes the exact build checkout and SHA-256 hashes.

## Runtime checks still needed

CI and component snapshots cannot prove Bitwig's actual MIDI/auxiliary routing,
automation feel, popup focus behavior or 125%/150% display scaling. Check those
with a fresh instance after replacing the old plugin and bridge.

The embedded host GUI delegate remains Win32-only. Native Linux validation and
Xvfb component rendering do not establish Linux host-window integration. These
are experimental beta builds, and the development PR remains unmerged.
