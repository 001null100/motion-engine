# Motion Engine v1 architecture

Motion Engine v1 is a Windows VST3 that turns a single simulated 2D body into coherent modulation. The user chooses one specialized motion model rather than assembling a graph.

## Core

- One normalized 2D world (`x/y` in `-1..1`).
- Fixed-step physics simulation, independent of the Bitwig controller update cadence.
- Ten specialized models: Orbit, Spring, Pendulum, Brownian, Drift, Bounce, Magnet, Explosion, Decay, Follower.
- Constraints: Free 2D, Horizontal, Vertical, Diagonal, Circle.
- Direct mouse drag/flick plus manual HIT impulse.
- Audio envelope/transient analysis feeds Follower and transient kicks.

## Sound-space sensing

Four circular Zones are first-class modulation sources. Each Zone has X, Y, radius and falloff. Zones never affect physics; they sense body proximity.

Derived sources include X/Y, X/Y velocity, speed, energy, radius, angle, impact, Zone 1-4, audio envelope and transient level.

## Outputs

Eight Motion Outputs each have:

- source
- output range (minimum / maximum)
- response curve
- smoothing

Each output can be mapped independently through the Bitwig Controller Extension.

## Bitwig bridge

The companion `.bwextension` keeps eight independently locked target parameters. Mapping is explicit: arm a slot, then move the desired target parameter. Unmapping restores Bitwig automation control.

The controller path intentionally publishes only the newest state. Physics itself is not limited to the roughly 44 Hz controller-application cadence measured by the prototype.

## Audio-rate fallback

The VST3 exposes eight mono auxiliary output buses named Motion 1 through Motion 8. Each carries the corresponding normalized Motion Output as a continuous control signal. In Bitwig these can be routed into an Audio Rate modulator when controller-thread modulation is unsuitable or unavailable.

## v1 UI

- Large 2D canvas with body, trail, velocity vector and Zones.
- Model and physical constraint controls above the canvas.
- Four model-specific controls whose names and meaning change per model.
- Global Time, Energy and Audio Kick controls.
- Eight compact output strips with source, range, curve, smoothing, MAP and CLEAR.
- Body drag/flick directly on canvas; Zone center and radius editing directly on canvas.

The UI deliberately avoids node graphs, patch cables and general-purpose modular simulation.