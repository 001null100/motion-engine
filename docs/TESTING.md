# Motion Engine v2 beta test pass

CI verifies the Windows CLAP build, deterministic physics suite, `clap-validator`, Bitwig bridge compile, and a safety gate that rejects `LastClickedParameter` target proxies. Runtime testing should focus on the actual Bitwig workflow.

## 1. Install

1. Copy `MotionEngine.clap` to `C:\Program Files\Common Files\CLAP\`.
2. **Replace** `MotionEngineBridge.bwextension` in `%USERPROFILE%\Documents\Bitwig Studio\Extensions\` with the newest release file.
3. Fully restart Bitwig Studio. If necessary, remove/re-add **Motion Engine > Software Bridge** in Settings > Controllers so Bitwig discards the old extension instance.
4. Add Motion Engine to an audio track.

### Critical bridge v0.3 safety check

Open the mapping/context menu on several unrelated Bitwig parameters and inspect them from any other controller tool you use.

- There must be **no** `Motion Engine Session ...` entries.
- There must be **no** `Motion Engine Bridge Target ...` entries.
- Motion Engine Bridge must not expose any arbitrary parameter target at all.
- Pressing legacy MAP controls from an older plug-in build must not change any Bitwig parameter.

If any old bridge-target entry remains after replacing the extension, restart Bitwig and remove/re-add the Motion Engine controller extension before reporting a regression.

## 2. Safe Motion Output routing

Motion Engine advertises eight mono CLAP auxiliary outputs, **Motion 1** through **Motion 8**. Each carries the corresponding Motion Output as a bipolar `-1..+1` signal with sample-rate interpolation.

For at least two lanes:

1. Configure Source, Minimum, Maximum, Curve and Smoothing in Motion Engine.
2. Expose/select the corresponding `Motion N` output in Bitwig's multi-output routing.
3. Add Bitwig's **Audio Rate** modulator to the destination device/track.
4. Select `Motion N` as its source.
5. Leave **Rectify** off for normal bipolar behavior.
6. Map Audio Rate to the destination parameter.
7. Confirm both lanes remain independent and responsive.

This is the canonical target-routing path. There should be no Controller Extension target mapping alias involved.

## 3. Canvas and model behavior

Check the currently stabilized interaction behavior:

- Constraints: Free 2D, Horizontal, Vertical, Diagonal and Circle.
- Orbit: exact reference ellipse; Orbit Speed changes travel rate without changing route geometry; HIT and throws can leave the route and settle back smoothly.
- Lissajous: body follows the displayed reference route without fractional-ratio phase snapping; HIT and throws can leave it temporarily and settle back.
- Pendulum: off-length drag/release returns continuously rather than snapping.
- Drift: does not stick indefinitely in corners.
- Bounce: positive gravity acts downward.
- Impulse: HIT produces the expected damped vector response and its preview remains accurate.
- Follower: stereo balance drives X while level/transients drive Y.
- Drag/flick, HIT, RESET and Zone editing remain responsive.

## 4. Native CLAP behavior

- Automate several Motion Engine parameters in Bitwig and verify playback and project reload.
- Apply Bitwig modulation directly to continuous Motion Engine parameters.
- Check native remote-control pages.
- Verify raw MIDI note-on still triggers HIT behavior.
- Verify main stereo audio passes through normally while Motion outputs are routed.
- Save/reload a project with non-default model, Zone and output settings and confirm state restores.

## 5. Multiple instances

Add a second Motion Engine instance and route different Motion outputs from each through separate Audio Rate modulators. The instances should remain independent. The companion bridge is diagnostic-only and must never acquire or write an arbitrary Bitwig target.

## What to report back

The most useful report is concise: whether the old bridge-target menu entries are completely gone, whether Motion 1-8 routing works cleanly, any model/canvas regression, project reload behavior, and any Control Script Console errors or crashes.

## 0.2.1 focused regression pass

Read `MOTION_ENGINE_0_2_1.md` for automated coverage and timing details. In Bitwig,
compare an audio-reactive lane at several buffer sizes, exercise native and raw
note hits, and close the editor while holding the body. Edit an output percentage
while audio runs, switch Zones while entering a radius, use FLIP on a reversed
range, and resize the canvas while its trail is visible. Check H/Enter outside
text fields, Escape during a canvas drag, and RESET without losing settings.

A missing optional bridge is not an error. The footer reports MIDI note-hit
activity, not arbitrary CC delivery. Motion 1-8 through Audio Rate remains the
only supported target-routing workflow. Existing Orbit/Lissajous HIT and drag
deviations must still leave and smoothly rejoin their reference paths.
