# Motion Engine v2 native-CLAP test pass

This pass is for the null-clap rewrite. CI already verifies that the Windows CLAP builds successfully and passes `clap-validator`; the goal here is to verify the real Bitwig workflow, physics behavior, eight-lane mapping, state, and audio-rate fallback.

## 1. Install

Use the newest **MotionEngine-Windows-CLAP** artifact from the rewrite PR, or the newest **Motion Engine v2 alpha** release after the rewrite is merged.

1. Copy `MotionEngine.clap` to:
   `C:\Program Files\Common Files\CLAP\`
2. Replace `MotionEngineBridge.bwextension` in:
   `%USERPROFILE%\Documents\Bitwig Studio\Extensions\`
3. Restart Bitwig Studio or force a plug-in rescan.
4. In **Settings > Controllers**, make sure **Motion Engine > Software Bridge** is present. Remove/re-add it if Bitwig retained an older extension instance.
5. Add **Motion Engine** to an audio track.

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

- **Orbit**: Radius sets path size, Ellipticity changes the aspect, Rotation rotates the ellipse, and Orbit Speed changes tangential travel speed.
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

Test at least these source types: X/Y Position, X/Y Velocity, Speed, Energy, Radius, Angle, Impact, Zone 1, Audio Envelope, and Transient.

Check that Minimum/Maximum can reduce or invert the range, Curve changes the response, Smoothing slows abrupt changes without freezing the lane, and the thin meter matches the resulting value.

## 6. Native CLAP parameter behavior

This is new to v2 and worth testing explicitly.

- Automate several Motion Engine controls in Bitwig and verify playback and project reload.
- Apply Bitwig modulation directly to continuous Motion Engine parameters and verify the physics responds without the editor values becoming corrupted.
- Check the Bitwig remote-control pages for World, Model, and Motion Output pages.
- Change parameters with the editor open and closed to catch GUI/host synchronization issues.

## 7. Bitwig bridge mapping

Map several lanes at the same time, ideally to a mix of Bitwig and third-party parameters.

For each lane:

1. Press **MAP**.
2. Hover the intended target, then actually drag/turn it slightly.
3. Confirm hovering unrelated parameters does not map them.
4. Confirm the strip shows the mapped parameter name.
5. Map another lane and verify the first mapping keeps working.
6. Press **X** on one lane and verify only that lane is cleared.

Bitwig's own modulators should continue to modulate the same target on top of Motion Engine's moving base value.

## 8. Automation restoration

Use a target with obvious existing automation.

1. Confirm its automation plays normally.
2. Map one Motion Output to it. Motion Engine should temporarily take over the base value.
3. Press **X** on that Motion Output.
4. The pre-existing automation should immediately resume without recreating or re-enabling the lane.

Repeat on one third-party plug-in parameter if convenient.

## 9. MIDI and audio interaction

### MIDI

Route notes to Motion Engine and play several notes. Raw MIDI note-on events should behave like HIT events. Verify that this does not mute or corrupt the main audio stream.

### Audio transient force

Use a rhythmic input and raise **Audio Kick**. Transients should disturb the Body. At zero Audio Kick they should stop imparting global force.

### Follower

Select **Follower**, feed real audio through Motion Engine, and compare silence, sustained audio, and rhythmic audio. It should chase a changing target with inertia rather than directly tracing an envelope.

## 10. Audio-rate fallback

Motion Engine advertises eight mono CLAP auxiliary outputs: **Motion 1** through **Motion 8**.

1. In the Motion Engine device, expose the desired `Motion N` output through Bitwig's multi-out routing.
2. Add Bitwig's **Audio Rate** modulator to the device/track you want to control.
3. Select the desired Motion Engine output as the source.
4. Leave **Rectify** off for the normal bipolar signal.
5. Map the Audio Rate modulator to a target parameter.

The aux signal is `-1..+1` and is interpolated between physics updates. This route bypasses the Controller Extension parameter-update ceiling and remains the manual fallback for sensitive/high-rate targets.

Check that main stereo audio still passes through normally while one or more aux outputs are enabled.

## 11. State, reload, and multiple instances

- Save the project with non-default model, Zone and output settings, close/reopen it, and confirm they restore.
- Check whether external Bitwig target mappings survive a project/Bitwig restart. Report exactly what survives.
- Add a **second Motion Engine instance on another track**. Map outputs from both instances to different targets and report whether they remain independent or fight.

The bridge protocol still uses the existing localhost transport, so multiple-instance behavior is an explicit boundary test rather than something to assume is solved.

## What to report back

The most useful report is concise: UI/canvas problems, model behavior, drag/flick/HIT, Zone editing, CLAP automation/modulation, bridge Map/Clear, MIDI/audio response, Motion 1-8 routing, project reload, two-instance behavior, and any Control Script Console errors or plug-in crashes.

Screenshots are especially useful for layout problems. A short screen recording is useful for physics that feels wrong.
