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

Bitwig's `LastClickedParameter` API behaves as a live hover tracker for native controls and does not expose a separate click event. Motion Engine therefore arms mapping and waits for an **actual parameter value change** before locking the target.

For both native Bitwig controls and third-party plug-in parameters:

1. Press **MAP TARGET**. Motion Engine should say `waiting for parameter movement`.
2. Move to the parameter you want.
3. Actually drag/turn/change it slightly.
4. Motion Engine should lock it and show `mapped` plus the parameter name.

Hovering a native Bitwig parameter without moving it must **not** map it.

A click that does not change the parameter value cannot be distinguished reliably through Bitwig's public controller API. For mapping, make a small drag/turn instead.

Press **UNMAP** before changing to an unrelated target. UNMAP now also calls Bitwig's `restoreAutomationControl()` for the mapped parameter so any pre-existing automation that was temporarily overridden can resume.

## 3. First sanity check

Use **Sine**, frequency **1 Hz**, bridge rate **120 Hz**.

Map it to **Tool > Pan** or another obvious native bipolar-ish control. Watch and listen for smooth motion. In the Motion Engine telemetry line record:

- `sent` rate
- `bridge rx` rate
- `applied` rate
- `worst gap`

Expected behavior: `sent` and `bridge rx` should be close to the requested rate. `applied` is the important measurement and may reveal Bitwig controller scheduling limits.

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
3. **Impulse** at 2 Hz. This generates an approximately 5 ms full-scale pulse. Note whether it is preserved, shortened, missed entirely, or smeared.
4. **Spring**. Set stiffness around 18 and damping around 2.4, then repeatedly press **KICK SPRING**. Listen for the overshoot and decay.

The impulse test is intentionally harsher than normal Motion Engine use. Two debug limitations matter when interpreting it:

- the VST meter redraws at only 20 Hz, so a 5 ms pulse will usually be invisible even when generated correctly;
- the bridge coalesces continuous values to the latest value before each Bitwig controller tick, so a short `1 -> 0` pulse that occurs entirely between two controller ticks can be lost by design.

With a controller path around 40-45 Hz, a literal 5 ms external parameter pulse is therefore not a realistic supported signal. Event-like impacts should instead become longer shaped motion, such as a spring kick/envelope, or use a separate future event path.

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

Direct controller writes to an automated Bitwig parameter intentionally put that parameter into Bitwig's temporary **automation override** state. While Motion Engine is mapped, absolute automation therefore does not combine with the Motion Engine base-value stream in the same clean way that Bitwig modulators do.

1. Unmap.
2. Draw obvious automation for a parameter over several bars.
3. Map Motion Engine to that same parameter.
4. Confirm Motion Engine takes over the base value while mapped.
5. Press **UNMAP**.
6. Confirm the original automation resumes automatically. If it does not, note whether Bitwig's global **Restore Automation Control** indicator is still armed.

The automation data itself should remain present; UNMAP should only release Motion Engine's temporary override.

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

**Usable with caveat:** controller scheduling tops out well below 120 Hz, but ordinary physics motion remains perceptually smooth, Bitwig modulators compose correctly with the moved base value, and sharp/event-like signals can be handled as shaped motion or through a separate path.

**Architectural fail:** update scheduling is visibly/audibly coarse at ordinary Motion Engine speeds, or continuous writes make normal project workflows unsafe even after automation control is correctly restored on unmap. In that case we pivot to another host-integration architecture before building the full plugin.
