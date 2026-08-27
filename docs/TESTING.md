# Motion Engine Bitwig bridge test

This prototype exists to answer one question: **is direct parameter control through a Bitwig Controller Extension smooth and predictable enough to be Motion Engine's primary output path?**

## 1. Install the release

1. Download the newest **prototype** release from this repository.
2. Extract `MotionEngine-Windows-VST3.zip`.
3. Copy the complete `Motion Engine.vst3` bundle to:
   `C:\Program Files\Common Files\VST3\`
4. Copy `MotionEngineBridge.bwextension` to:
   `%USERPROFILE%\Documents\Bitwig Studio\Extensions\`
   Create `Extensions` if it does not exist.
5. Restart Bitwig Studio.
6. Open **Settings > Controllers**, choose **Add Controller**, and add:
   **Motion Engine > Software Bridge**.
7. Add **Motion Engine** to an audio track. The prototype passes audio through unchanged.
8. Open its UI. Within about one second it should say **Bridge online**.

If the bridge does not appear, press `Ctrl+Enter`, search for **Control Script Console**, open it, and confirm you see:
`Motion Engine Bridge listening on UDP 127.0.0.1:19782`

## 2. Learn the mapping workflow

### Third-party plug-in parameter, for example Serum 2

1. Open the target plug-in GUI.
2. Physically click or drag the parameter you want to test so Bitwig receives an automation-touch event.
3. Return to Motion Engine and press **MAP TARGET**.
4. Motion Engine should show the parameter name and `mapped`.

### Native Bitwig parameter

1. Make sure your mouse is not currently hovering a parameter.
2. In Motion Engine press **MAP TARGET**. It should say `waiting for target`.
3. Move the mouse over the native Bitwig parameter you want to test.
4. The extension should lock it automatically and Motion Engine should switch to `mapped`.

Press **UNMAP** before changing to an unrelated target.

## 3. First sanity check

Use **Sine**, frequency **1 Hz**, bridge rate **120 Hz**.

Map it to **Tool > Pan** or another obvious native bipolar-ish control. Watch and listen for smooth motion. In the Motion Engine telemetry line record:

- `sent` rate
- `bridge rx` rate
- `applied` rate
- `worst gap`

Expected behavior: `sent` and `bridge rx` should be close to 120 Hz. `applied` is the important measurement and may reveal Bitwig controller scheduling limits.

## 4. Rate sweep

Keep **Sine / 1 Hz** and use the same target. Test each requested rate for at least 10 seconds:

| Requested | Sent | Bridge RX | Applied | Worst gap | Audible/visible result |
|---:|---:|---:|---:|---:|---|
| 30 Hz | | | | | |
| 60 Hz | | | | | |
| 120 Hz | | | | | |
| 250 Hz | | | | | |
| 500 Hz | | | | | |
| 1000 Hz | | | | | |

Do not chase 1000 Hz for its own sake. We care about the lowest rate that is perceptually transparent and stable.

## 5. Frequency sweep

Use whichever bridge rate looked healthy in section 4. On a target that exposes stepping clearly, test Sine at:

- 0.1 Hz
- 1 Hz
- 5 Hz
- 10 Hz
- 20 Hz
- 30 Hz

Good targets: filter cutoff with resonance, oscillator/fine pitch, pan. Reverb mix is a bad diagnostic target because the effect can hide stepping.

## 6. Discontinuity torture test

With the same target and rate, test:

1. **Ramp** at 2 Hz. Listen specifically at the wrap from 1 back to 0.
2. **Step** at 2 Hz. Confirm transitions happen promptly and do not produce strange bursts of intermediate values.
3. **Impulse** at 2 Hz. This emits an approximately 5 ms full-scale pulse. Note whether it is preserved, shortened, missed entirely, or smeared.
4. **Spring**. Set stiffness around 18 and damping around 2.4, then repeatedly press **KICK SPRING**. Listen for the overshoot and decay.

The impulse test is intentionally harsher than normal Motion Engine use.

## 7. Target matrix

Repeat the useful parts of the test on these targets:

1. **Bitwig Tool pan**
2. **Bitwig stereo width** or another native continuous parameter
3. **Serum 2 filter cutoff**, preferably with resonance so stepping is obvious
4. **Serum 2 fine pitch / oscillator pitch**
5. **Distortion drive**
6. **Delay time**

For each target, note whether the target itself appears to smooth incoming host parameter changes. Different plug-ins can behave differently even at the same bridge rate.

## 8. Automation and modulation interaction test

This is architecturally more important than raw speed.

### Existing Bitwig modulation

1. Map Motion Engine to a native parameter.
2. Add a normal Bitwig LFO modulator to the **same parameter** with a clearly visible amount.
3. Start Motion Engine Sine at a slow rate.
4. Observe whether Motion Engine moves the **base value** while the LFO remains applied around it, fights/resets the modulation, or causes any UI/state oddities.

### Existing automation

1. Unmap.
2. Draw obvious automation for a parameter over several bars.
3. Map Motion Engine to that same parameter.
4. Test once with automation playback active.
5. Arm automation recording only if you deliberately want to test recording behavior. Motion Engine does **not** call `Parameter.touch(true)`, so it should not intentionally write touch automation by itself.
6. Stop playback, unmap, and check whether the original automation/base value was preserved or overwritten.

Make a note of the exact automation mode Bitwig was using.

## 9. What to send back

For a useful first report, send:

- Bitwig version
- audio buffer size and sample rate
- CPU model only if convenient
- rate-sweep table from section 4
- the highest Sine frequency that still sounded completely smooth on Serum cutoff or pitch
- what happened to the 5 ms Impulse
- what happened when Bitwig modulation and automation already existed on the mapped target
- any Control Script Console errors

A screen recording is useful if the parameter visibly stair-steps, but the telemetry numbers and listening result matter more.

## Pass/fail interpretation

**Strong pass:** 120-250 Hz is stable and perceptually transparent on sensitive parameters; spring/impact behavior is convincing; base-value interaction with Bitwig modulation is sane.

**Usable with caveat:** ordinary motion is excellent but very sharp impacts are softened or missed. We keep the controller bridge for normal outputs and design a special path for transient-rate signals later.

**Architectural fail:** update scheduling is visibly/audibly coarse at ordinary Motion Engine speeds, or continuous writes destroy/fight automation/modulation in a way that makes the workflow unsafe. In that case we pivot to an audio/native-modulator bridge before building the full plugin.
