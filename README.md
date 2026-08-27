# Motion Engine

Motion Engine is a bespoke Windows VST3 that turns a stateful 2D physics simulation into coherent modulation.

Instead of drawing LFO curves, you throw a virtual Body into a motion model and map its position, velocity, energy, impacts, audio response, or proximity to Zones onto other parameters.

## v1 features

- Ten specialized motion models: Orbit, Spring, Pendulum, Brownian, Drift, Bounce, Magnet, Explosion, Decay, Follower.
- Free 2D, horizontal, vertical, diagonal, and circular physical constraints.
- Direct Body drag/flick and manual HIT interaction.
- Audio envelope/transient analysis plus MIDI note-on hits.
- Four draggable circular Zones with editable radius/falloff.
- Fifteen modulation sources including X/Y, velocity, speed, energy, radius, angle, impact, Zones, audio envelope, and transient level.
- Eight independent Motion Outputs, each with source, min/max range, response curve, and smoothing.
- Companion Bitwig Controller Extension for one-click target mapping.
- Eight mono VST3 auxiliary outputs for manual audio-rate modulation fallback.

## Bitwig integration

The normal workflow uses `MotionEngineBridge.bwextension`.

1. Press **MAP** on one Motion Output.
2. Move/drag the Bitwig or plug-in parameter you want that lane to control.
3. The target is locked to that lane and Motion Engine begins moving its base parameter value.
4. Press **×** to clear the target and restore Bitwig automation control.

Bitwig's own modulators can still modulate the same target on top of the base value moved by Motion Engine.

The controller path is intentionally used for organic/physics-speed movement. The prototype measured roughly 44 applied parameter updates per second in Bitwig; the internal physics simulation runs independently at 240 Hz.

## Audio-rate fallback

The VST3 exposes auxiliary output buses **Motion 1** through **Motion 8**. Each bus carries the corresponding Motion Output as a bipolar `-1..+1` control signal with sample-rate interpolation.

In Bitwig, route one of those buses into an **Audio Rate** modulator when the controller bridge is unavailable or when a target needs a faster modulation path.

## Canvas controls

- Drag the Body and release it to throw it with momentum.
- Drag a Zone center to move it.
- Drag a Zone ring to change its radius.
- Double-click the canvas or press **HIT** to disturb the simulation.
- Select a Zone above the canvas for exact radius/falloff controls.

## Build

The project uses C++20, JUCE 9, CMake, and a Java 21 Bitwig Controller Extension. GitHub Actions builds the Windows VST3 and `.bwextension`; pushes to `main` publish an automated prerelease.

See `docs/V1_ARCHITECTURE.md` for the architecture and `docs/TESTING.md` for the current test checklist.
