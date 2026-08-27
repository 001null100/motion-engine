# Motion Engine v1 test pass

This is the first usable Motion Engine build. The goal of this pass is to test the actual physics workflow, eight-lane Bitwig mapping, and the manual audio-rate fallback.

## 1. Install

Download the newest **Motion Engine v1 alpha** release.

1. Extract `MotionEngine-Windows-VST3.zip`.
2. Replace the complete `Motion Engine.vst3` bundle in:
   `C:\Program Files\Common Files\VST3\`
3. Replace `MotionEngineBridge.bwextension` in:
   `%USERPROFILE%\Documents\Bitwig Studio\Extensions\`
4. Restart Bitwig Studio.
5. In **Settings > Controllers**, make sure **Motion Engine > Software Bridge** is present. Remove/re-add it if Bitwig retained an older extension instance.
6. Add **Motion Engine** to an audio track.

The footer should report **Bitwig bridge** rather than **Bitwig bridge not seen**.

## 2. Canvas interaction

Start with **Spring** and **Free 2D**.

- Drag the bright Body around the canvas. It should follow the pointer directly.
- Release while moving. The Body should inherit your throw velocity and continue with momentum.
- Press **HIT** or double-click the canvas. The Body should be kicked and then settle according to the Spring controls.
- Press **RESET**. The current model should return to a sensible starting state.
- The trail and velocity line should follow the Body without obvious visual glitches.

Then try the physical constraints:

- **Horizontal**: Body remains on Y=0.
- **Vertical**: Body remains on X=0.
- **Diagonal**: Body moves on the bottom-left to top-right diagonal.
- **Circle**: Body remains on a circular path.

## 3. Motion model sanity pass

Switch through every model and move its four dedicated controls through a useful range. The labels should change with the model.

- **Orbit**: sustained curved/orbital motion around the center; Speed, Central Pull, Ellipticity and Precession should be clearly different controls.
- **Spring**: HIT/throw produces overshoot and damped settling around the anchor.
- **Pendulum**: Body remains on the pendulum arc and responds to Length, Gravity, Damping and Drive.
- **Brownian**: correlated wandering rather than frame-to-frame white jitter.
- **Drift**: slow flowing movement with obvious inertia/curl.
- **Bounce**: free travel and visible collisions with the world boundary; Restitution changes rebound behavior.
- **Magnet**: attraction/repulsion around the center with polarity and orbit-bias effects.
- **Explosion**: HIT produces a strong outward event followed by drag/return behavior.
- **Decay**: HIT injects energy that progressively dissipates.
- **Follower**: incoming audio moves the Body; level primarily affects vertical target while stereo balance contributes horizontal target.

Report any model that explodes numerically, sticks to a boundary, feels redundant, or has a control that appears to do nothing.

## 4. Zones

Four Zones are visible as colored circular fields.

- Drag a Zone's center point to move it.
- Drag near its outer ring to resize it.
- Select a Zone from **Zone edit** and change Radius/Falloff numerically.
- The Zone should brighten as the Body moves deeper into it.
- Assign one Motion Output to `Zone 1` (or another Zone). Its output meter should rise as the Body enters the Zone and fall as it leaves.

Zones are sensors only. Moving into a Zone must not alter the Body's trajectory.

## 5. Eight Motion Outputs

Each output strip has Source, Minimum, Maximum, Curve, Smoothing, MAP and Clear.

Test at least these source types:

- X Position
- Y Position
- X/Y Velocity
- Speed
- Energy
- Radius
- Angle
- Impact
- Zone 1
- Audio Envelope
- Transient

Check:

- Minimum/Maximum can reduce or invert the output range by setting Minimum above Maximum.
- Curve types visibly change the response.
- Smoothing slows abrupt output changes without freezing the lane.
- The thin meter at the left of each strip matches the resulting value.

## 6. Bitwig mapping

Map several lanes at the same time, ideally to a mix of Bitwig and third-party parameters.

Example:

- Motion 1 / X Position -> Tool Pan
- Motion 2 / Y Position -> stereo width
- Motion 3 / Speed -> distortion drive
- Motion 4 / Zone 1 -> reverb mix
- Motion 5 / Impact -> filter movement

For each lane:

1. Press **MAP**.
2. Hover the intended target, then actually drag/turn it slightly.
3. Confirm hovering unrelated parameters does not map them.
4. Confirm the strip shows the mapped parameter name.
5. Map another lane and verify the first mapping keeps working.
6. Press **×** on one lane and verify only that lane is cleared.

Bitwig's own modulators should continue to modulate the same target on top of Motion Engine's moving base value.

## 7. Automation restoration

Use a target with obvious existing automation.

1. Confirm its automation plays normally.
2. Map one Motion Output to it. Motion Engine should temporarily take over the base value.
3. Press **×** on that Motion Output.
4. The pre-existing automation should immediately resume without recreating or re-enabling the lane.

Repeat on one third-party plug-in parameter if convenient.

## 8. MIDI and audio interaction

### MIDI

Route notes through/to Motion Engine and play several notes. Note-on events should behave like HIT events. Verify that this does not mute or otherwise corrupt the main audio stream.

### Audio transient force

Use a rhythmic input and raise **Audio Kick**. Transients should disturb the Body. At zero Audio Kick they should stop imparting global force.

### Follower

Select **Follower**, feed real audio through Motion Engine, and compare silence, sustained audio, and rhythmic audio. It should chase a changing target with inertia rather than directly tracing an envelope.

## 9. Audio-rate fallback

Motion Engine advertises eight mono auxiliary VST outputs: **Motion 1** through **Motion 8**. Bitwig exposes multichannel plug-in outputs through its Multi-out chains.

1. In the Motion Engine device, open the **Multi-out** selector (double-arrow button).
2. Add/enable the desired `Motion N` output chain.
3. Add Bitwig's **Audio Rate** modulator to the device/track you want to control.
4. In Audio Rate's source chooser, select the track containing Motion Engine, then **Chains**, then the desired `Motion N` source.
5. Leave **Rectify** off for the normal bipolar signal.
6. Map the Audio Rate modulator to a target parameter.

The aux signal is `-1..+1` and is smoothly interpolated between physics updates. This route bypasses the Controller Extension parameter-update ceiling and is the manual fallback for sensitive/high-rate targets.

Check that the main stereo audio from Motion Engine still passes through normally while one or more aux chains are enabled.

## 10. State, reload, and multiple instances

These are important because the original bridge spike only proved a single live instance.

- Save the project with several Motion Engine parameter settings, close/reopen it, and confirm model/Zone/output settings restore.
- Check whether external Bitwig target mappings survive a project/Bitwig restart. Report exactly what survives; this has not yet been proven by the prototype.
- Add a **second Motion Engine instance on another track**. Map outputs from both instances to different targets and report whether they remain independent or fight. This is an explicit v1 boundary test.

Do not assume a failure here is user error. If two instances collide, the bridge protocol needs per-instance sessions.

## What to report back

The most useful report is concise:

- UI/canvas problems
- model(s) that feel wrong or redundant
- drag/flick/HIT behavior
- Zone editing/proximity behavior
- how many simultaneous output mappings you tested
- whether per-lane Map/Clear and automation restoration worked
- MIDI and audio-response behavior
- whether Motion 1-8 appear in Bitwig Multi-out and whether Audio Rate can use them
- project reload behavior
- two-instance behavior
- any Control Script Console errors or plug-in crashes

Screenshots are especially useful for layout/visual problems. A screen recording is useful for physics that feels wrong.