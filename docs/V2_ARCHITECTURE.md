# Motion Engine v2 architecture

This document describes the native-CLAP rewrite on `null-clap-rewrite`. It is the handoff reference for future work; `V1_ARCHITECTURE.md` describes the older JUCE AudioProcessor/VST3 implementation.

## Design goals

Motion Engine should behave like a native CLAP modulation tool, not a JUCE VST wrapped in another format. The rewrite keeps JUCE only where it is useful: drawing and embedding the editor. Audio processing, parameters, state, events, ports, host integration, physics and bridge networking are independent of JUCE.

The practical goals are:

- native CLAP automation/modulation and state semantics;
- stable parameter IDs across builds;
- no locks, allocation, networking or GUI work on the audio thread;
- a host-independent physics core that can be tested without a DAW;
- eight audio-rate modulation outputs as a fallback when controller-driven parameter updates are too slow;
- Bitwig target mapping that is isolated per Motion Engine instance;
- enough automated validation that routine refactors do not require discovering obvious failures in Bitwig.

## Layer map

### `MotionEnginePlugin`

`Source/MotionEnginePlugin.*` owns the CLAP-facing plug-in lifecycle through `nullclap::Plugin`.

Responsibilities:

- register all parameters and stable IDs;
- expose stereo input/output plus eight mono `Motion 1..8` output ports;
- expose a raw MIDI note input;
- translate effective CLAP parameter values into `motion::Parameters` for the core;
- analyse incoming audio spans for RMS/stereo balance/transient input;
- pass main audio through unchanged;
- interpolate each normalized motion output to bipolar `-1..+1` audio-rate output samples;
- treat MIDI note-on as a physics HIT;
- attach the JUCE GUI delegate;
- rely on null-clap for parameter automation/modulation, state and host callbacks.

The project currently pins null-clap commit `253cc44d0d5625519208a89177d9f7f4d73ccffd`, which includes bounds-safe process event handling and writable-parameter state restoration.

### `MotionEngineCore`

`Source/MotionEngineCore.*` is plain C++ with no JUCE or CLAP dependency.

The core runs a fixed 240 Hz simulation and contains:

- Orbit
- Spring
- Pendulum
- Brownian
- Drift
- Bounce
- Magnet
- Explosion
- Decay
- Follower

It also owns constraints, audio/transient response, four proximity Zones, source extraction, response curves, smoothing and the eight final normalized outputs.

GUI interactions are handed to the audio thread through atomics: reset, HIT, body drag and throw. Published snapshots are atomic so the GUI and bridge never take a mutex from the audio thread.

### `BridgeEngine`

`Source/BridgeEngine.*` is the plug-in side of the Bitwig mapping bridge. It uses a normal C++ worker thread and native UDP sockets. It never runs networking from the audio callback.

The bridge sends output values at a nominal 120 Hz. This is intentionally independent from the 240 Hz physics simulation. Bitwig's controller API applies external parameter writes at a lower practical ceiling, measured around 44 Hz in the prototype; the audio-rate outputs remain the fallback for targets that need more bandwidth.

Each plug-in instance creates a random 64-bit hexadecimal session ID for its lifetime.

The bridge marks itself offline when no matching telemetry arrives for 2.5 seconds. Stale mapping/armed state is cleared from the editor rather than leaving a dead bridge looking healthy.

### Bitwig Controller Extension

`bridge/src/main/java/dev/nullexo/motionengine/` contains the Java 21 Controller Extension.

The v2 extension preallocates eight isolated session banks. Each bank owns eight `LastClickedParameter` trackers and eight target slots, so simultaneous Motion Engine instances no longer share one global set of targets.

Only one target capture can be armed globally at a time. Existing mapped targets stay locked. Unused trackers stay locked so merely hovering parameters cannot accidentally remap anything.

When a plug-in instance sends `BYE`, its bank restores automation control for mapped targets, locks its trackers and becomes reusable.

Current explicit capacity: 8 simultaneous live Motion Engine instances per Controller Extension instance.

## ME3 bridge protocol

All packets are UTF-8 text sent over localhost UDP port `19782`.

### Plug-in to extension

```text
ME3|VALUES|<session>|<sequence>|<v1>|<v2>|...|<v8>
ME3|MAP|<session>|<slot>
ME3|UNMAP|<session>|<slot>
ME3|BYE|<session>
```

- `<session>` is an 8-32 character hexadecimal identifier; Motion Engine currently emits 16 characters.
- `<sequence>` is an unsigned increasing integer local to the plug-in instance.
- values are normalized `0..1`.
- slots are zero-based `0..7`.

### Extension to plug-in

```text
ME3|STATUS|<session>|<rxHz>|<appliedHz>|<worstGapMs>|<mappedMask>|<armedMask>|<name1>~...~<name8>
```

Telemetry is sent independently to every active session's UDP peer. A Motion Engine instance ignores status packets for any other session.

The names field sanitizes protocol delimiters so target names cannot corrupt packet parsing.

## Target mapping behavior

The intended mapping interaction is deliberately click/change based, not hover based:

1. The user presses `MAP` on a Motion Output.
2. The extension unlocks only that lane's `LastClickedParameter` tracker.
3. Hovering a parameter may change the candidate, but does not commit it.
4. The candidate is allowed to settle for two controller ticks.
5. A real value change above the epsilon locks the target.
6. The lane begins applying Motion Engine values to that target.
7. `X` calls `restoreAutomationControl()` and clears only that lane.

If another Motion Engine instance arms a lane while one is already waiting for capture, the newer request becomes the sole armed capture. This prevents two instances from silently grabbing the same mouse interaction.

## GUI architecture

`Source/JuceGuiDelegate.*` embeds a JUCE `Component` inside the CLAP host window. JUCE is not used for processor, parameter or state plumbing.

`Source/PluginEditor.*` talks directly to `MotionEnginePlugin` using null-clap GUI parameter gestures:

- `beginParameterGesture()`
- `setParameterFromGui()`
- `endParameterGesture()`

The editor retains the v1 interaction model: draggable body, throw velocity, Zones, Orbit path guide, per-model controls, eight output strips, bridge mapping controls and live meters.

## Parameter/state model

Stable IDs live in `Source/ParameterIds.hpp` and are generated with `nullclap::stableId()` from permanent text keys. Do not rename those keys casually; they are the identity used by hosts and saved projects.

Continuous controls are automatable and modulatable by default. Choice parameters are stepped enums and automatable.

State persistence is handled by null-clap. Motion Engine's extra-state hook currently only rehydrates the core after parameter state has been restored; the actual physics body position is intentionally transient and resets to a sensible state for the restored model.

External Bitwig target mappings are not part of CLAP state. They belong to the Controller Extension and still require runtime verification for project/Bitwig restart persistence.

## Threading model

### Audio thread

Allowed:

- read effective CLAP parameters;
- run audio analysis;
- run the fixed-step physics core;
- copy audio;
- write auxiliary modulation outputs;
- consume atomically queued HIT/drag/reset requests.

Not allowed:

- GUI calls;
- UDP/socket work;
- Java/Bitwig bridge work;
- blocking locks;
- filesystem work.

### GUI/message thread

- paints the simulation snapshot;
- sends native CLAP parameter gestures;
- queues body interaction requests into the core;
- reads bridge telemetry snapshots.

### Bridge worker thread

- sends `VALUES`, `MAP`, `UNMAP`, `BYE` packets;
- receives `STATUS` telemetry;
- updates the bridge status snapshot.

### Bitwig controller thread

All target parameter writes, LastClickedParameter locking and automation restoration happen in the extension's scheduled `controlTick()`.

The UDP receive loop only parses/copies bridge data into atomic/session state for that controller tick to consume.

## Automated gates

The Windows GitHub Actions job performs:

1. CMake configure.
2. Release build of Motion Engine and the core test executable.
3. `ctest` physics smoke pass.
4. `clap-validator` validation of `MotionEngine.clap`.
5. Maven build of `MotionEngineBridge.bwextension`.
6. packaging and artifact upload.

`tests/MotionEngineCoreTests.cpp` currently runs all ten models for ten simulated seconds each with audio response, periodic HITs, drag/throw interaction, output inversion and multiple smoothing settings. It then exercises all four non-free constraints. The test fails on non-finite physics, world-bound escapes, or any normalized zone/output/snapshot signal outside `0..1`.

## Runtime checks still requiring Bitwig

Automated tests cannot prove host UX. Before merging the rewrite, verify:

- CLAP editor embeds, opens/closes and resizes reliably;
- host automation changes Motion Engine parameters correctly;
- native CLAP modulation affects continuous parameters correctly;
- project reload restores parameter state;
- raw MIDI note-on reaches HIT;
- Motion 1..8 are visible and usable through Bitwig multi-out routing;
- Audio Rate can use those outputs as bipolar sources;
- bridge MAP waits for a real target value change rather than hover;
- Clear restores existing target automation;
- two or more Motion Engine instances maintain independent mappings and values;
- deleting an instance releases its bridge session without disturbing another instance;
- restarting/removing the Controller Extension causes the plug-in footer to go offline rather than showing stale bridge state;
- restarting the extension reconnects active plug-in sessions;
- external target mappings across project/Bitwig restart are characterized explicitly.

## Do not regress these choices

- Do not reintroduce JUCE `AudioProcessor` or APVTS just to simplify UI binding.
- Do not perform bridge networking from the audio thread.
- Do not collapse ME3 back into a single global bridge target bank.
- Do not make MAP commit merely because the mouse hovered a parameter.
- Do not remove the audio-rate fallback solely because the Controller Extension path works for ordinary movement.
- Do not change stable parameter ID strings without treating it as a saved-project compatibility change.
