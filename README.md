# Motion Engine

Motion Engine is a bespoke Windows CLAP plug-in that turns a stateful 2D physics simulation into coherent modulation.

Instead of drawing LFO curves, you throw a virtual Body into a motion model and use its position, velocity, energy, impacts, audio response, or proximity to Zones as modulation sources.

## v2 beta

The current v2 build runs directly on [`null-clap`](https://github.com/001null100/null-clap). JUCE remains only as the drawing/window toolkit for the editor; the processor lifecycle, parameters, state, MIDI, audio ports and host integration are native CLAP.

- Ten specialized motion models: Orbit, Spring, Pendulum, Brownian, Drift, Bounce, Lissajous, Impulse, Decay, Follower.
- Free 2D, horizontal, vertical, diagonal, and circular constraint projections that preserve the underlying model motion.
- Direct Body drag/flick and manual HIT interaction.
- Audio envelope/transient analysis plus raw MIDI note-on hits.
- Four draggable circular Zones with editable radius/falloff.
- Fifteen modulation sources including X/Y, velocity, speed, energy, radius, angle, impact, Zones, audio envelope, and transient level.
- Eight independent Motion Outputs, each with source, min/max range, response curve, and smoothing.
- Eight mono CLAP auxiliary outputs for safe audio-rate modulation in Bitwig.
- Native CLAP automation/modulation and remote-control pages.
- Pure C++ physics core with lock-free atomic snapshot publication.
- DPI-aware embedded editor, explicit constraint/model guides, and hardened discrete selector interaction.

### Motion models

- **Orbit** — exact elliptical reference route with physical HIT/throw deviations that settle back to the path.
- **Spring** — elastic attraction to an offset anchor with optional swirl.
- **Pendulum** — gravity-driven tether that can stretch naturally when thrown.
- **Brownian** — correlated random motion with inertia and directional bias.
- **Drift** — smooth wandering current with soft wall avoidance.
- **Bounce** — ballistic movement with gravity, restitution and impact chaos.
- **Lissajous** — exact coupled X/Y reference route with physical deviations and continuous fractional-ratio phase.
- **Impulse** — HIT launches a damped vector impulse that rings through center and settles.
- **Decay** — HIT starts a shrinking orbital ring with controllable wobble.
- **Follower** — stereo balance drives X; level and transients drive Y.

## Bitwig integration

### Safe modulation route

Motion Engine exposes auxiliary output ports **Motion 1** through **Motion 8**. Each carries the corresponding Motion Output as a bipolar `-1..+1` control signal with sample-rate interpolation.

For each target in Bitwig:

1. Expose/select the desired **Motion N** auxiliary output from Motion Engine.
2. Add Bitwig's **Audio Rate** modulator to the device or track you want to control.
3. Choose **Motion N** as the Audio Rate source.
4. Leave **Rectify** off for the normal bipolar signal.
5. Map the Audio Rate modulator to the destination parameter.

This path is both safer and faster than the retired controller-target bridge. It does not create controller mapping aliases, cannot capture Motion Engine's own parameters, and is not limited by the Controller Extension parameter-update rate.

### Why direct bridge mapping was retired

Older v2 beta builds used Bitwig `LastClickedParameter` objects to implement one-click arbitrary target mapping. Bitwig deliberately exposes those objects as user-facing controller mapping targets. Other controller tools could therefore see and interact with Motion Engine's supposedly internal target aliases, and the capture path could also lock onto internal parameters. That made the design unsafe for real projects.

Bridge **v0.3.0** creates **zero** `LastClickedParameter` objects and never writes arbitrary Bitwig parameters. It remains only as a localhost heartbeat/diagnostic companion for the plug-in. The CI pipeline rejects any future bridge source that reintroduces `LastClickedParameter` target proxies.

If an older `MotionEngineBridge.bwextension` is installed, replace it with the v0.3.0 file from the newest release and restart Bitwig. The old `Motion Engine Session ...` or `Motion Engine Bridge Target ...` entries should disappear after Bitwig reloads the controller extension.

## Canvas controls

- Drag the Body and release it to throw it with momentum.
- Drag a Zone center to move it.
- Drag a Zone ring to change its radius.
- Double-click the canvas or press **HIT** to disturb/retrigger the current model.
- Select a Zone above the canvas for exact radius/falloff controls.

## Architecture

The rewrite deliberately separates the layers:

- `MotionEnginePlugin`: native CLAP lifecycle, parameters, MIDI, audio I/O and state.
- `MotionEngineCore`: host-independent C++ physics and modulation generation.
- `BridgeEngine`: JUCE-free UDP heartbeat/diagnostic sender and receiver.
- `PluginEditor` + `JuceGuiDelegate`: JUCE GUI only, talking to native CLAP parameter gestures.
- `null-clap`: reusable CLAP framework shared with the other bespoke plug-ins.

## Build

The project uses C++20, CMake, `null-clap`, JUCE 9 for GUI only, and Java 21 for the Bitwig Controller Extension.

GitHub Actions builds `MotionEngine.clap`, validates it with `clap-validator`, runs the deterministic model/constraint regression suite, verifies that the Bitwig bridge contains no `LastClickedParameter` target proxies, builds `MotionEngineBridge.bwextension`, and packages both. Pushes to `main` publish an automated v2 beta prerelease only after validation succeeds.

See `docs/TESTING.md` for the current test pass. `docs/V1_ARCHITECTURE.md` documents the original JUCE/VST3 implementation for historical reference.