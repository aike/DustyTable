[日本語](README.md) | English

# DustyTable

An audio plugin (VST3 / Standalone, JUCE 8) that generates vinyl record noise by physical modelling. It computes the sound of a stylus tracing the groove and adds it to the input signal.

## Features

- **Adds only the noise, leaving the tone of your source untouched** The input signal gets no EQ, saturation, wow & flutter or any other colouring. The only things that touch it are the Dry gain and the mute while the stylus is out of the groove (when a scratch makes the stylus jump, the source dips very slightly, exactly as the physical model says). Shape the sound with your own favourite compressor, EQ and saturator.
- **A physical model, not samples** The crackle of dust, the thump of scratches and the fizz of static electricity are generated from the contact between stylus tip and groove walls, the cantilever, the tonearm, an MM cartridge and RIAA playback EQ. No pattern ever repeats.
- **Make the record dirty yourself** With the dust, nail and eraser tools you can let dust settle on the spinning record and cut or remove scratches wherever you like. Scratches are saved with the project, and replaying the same passage in the DAW plays them at the same moments.
- **Even the turntable's handling noises** START/STOP, raising and lowering the arm, the motors and gears and the arm rest are all sounded by physical models built from the parts' dimensions and materials. The cartridge noise (Noise) and the airborne mechanical sound (Mechanics) have separate levels.
- **Choose the player setup** 33/45 rpm, disc size (12"/10"/7"), stylus (Conical/Elliptical) and tracking force all change the sound. The stylus moves inward as it plays, and at the end of the side the arm returns by itself.

## Installation

Download the latest version from [GitHub Releases](../../releases). Builds are provided for Windows (64-bit).

1. Unzip `DustyTable-<version>-Windows-x64.zip`.
2. VST3 plugin: copy `DustyTable.vst3` (the whole folder) from the `VST3` folder to `C:\Program Files\Common Files\VST3` (administrator rights required). Rescan plugins in your DAW and it is available as an effect.
3. Standalone: `DustyTable.exe` in the `Standalone` folder runs from anywhere (choose the audio input and output in its settings).

The executables are not code-signed, so Windows SmartScreen may show a warning the first time. If it does, choose "More info" → "Run anyway".

## Usage

1. Insert it on a track as an effect (the noise is added to the original signal).
2. The START button starts the turntable and lowers the arm at the Drop point. STOP returns the arm to its rest.
3. Add dust and scratches with the tools at the top left of the record, and adjust their character with the DUST, SCRATCH and STATIC ELECTRICITY knobs.
4. Balance noise, mechanical sound and the original signal with Noise, Mechanics and Dry in the OUTPUT section.

Raising and lowering the arm is driven by the "Start / Stop" parameter (automatable; the START and STOP buttons in the GUI) and is independent of the DAW's transport. The deck is modelled on an automatic player. When a DAW project is opened, the arm starts in its saved state rather than replaying the actions.

- **Stylus travel** While the stylus sits in the groove it moves slowly inward as the record turns (from the outer to the inner end of the recorded band over the Side length; this does not depend on the host's transport, only on the stylus being in the groove and the record turning). At the end of the recorded band the deck stops by itself, just like pressing STOP, and the Start / Stop parameter switches to Stop. Once the arm is back on its rest, the next START sets it down at the Drop point. The current stylus position is shown below the record as "Stylus xx %". The stylus position is saved with the project; if the arm was on the record, playback resumes from there when the project is opened. While the platter runs at full speed, the record's angle follows the host's playback position, so replaying the same passage plays the scratches at the same moments (the stylus position, however, does not follow the playback position). The record keeps turning while the host is stopped.
- **Raising and lowering the arm in place (UP/DOWN)** Each press of the UP/DOWN button above START/STOP raises the arm where it is or lowers it again. The button itself has no state; the arm mechanism remembers whether it is up (not automatable). A second small motor turns a cue cam that pushes up the same lifter rod (up in about 1 second). It comes down at the Cue damping speed and lands under its own weight for the last bit. While the arm is up, a third motor turns it through a worm gear and slipping clutch and moves it slowly, at about 8 mm/s, to the Drop point (moving the Drop point while the arm is up sends the arm there; if you move the Drop point while playing, the arm moves once UP has fully raised it). Lowering it continues playback from that position. Pressing DOWN while it is moving stops it where it is and lowers it. Nothing happens while the arm is on its rest. Pressed while the arm is still coming down after START, it raises the arm once it has landed. STOP while the arm is up returns it to the rest as usual. The record keeps turning while the arm is up, and the stylus position does not advance. Whether the arm is up and where it is are saved with the project, and it reopens in that state. The cue and traverse motors, their gears and limit switches are sounded by the physical model too.
- **Platter rotation** START spins the platter up (full speed in about 0.8 s, within the 1 s wait before the arm starts moving). After STOP, once the arm is back on its rest and the cam has stopped, the platter coasts to a halt over about 2.5 s. The angle lost while it was stopped stays as an offset from the playback position, which the record then follows.

## Parameters

| Section | Parameters |
|---|---|
| Deck | 33/45, disc size (12"/10"/7"), stylus (Conical/Elliptical, chosen with an illustrated switch showing the tip's footprint in the groove seen from above: round for Conical, narrow for Elliptical), tracking force (g), Drop point (where START sets the arm down, %; parameter ID `position`. While it is being changed, a see-through arm and the groove it would land on are shown at that position; they disappear when you let go of the knob, or after about 0.8 s without change when set by the mouse wheel or automation), Side (length of one side, minutes), Cue damping (descent time of the oil-damped lifter, seconds), Start / Stop (the START and STOP buttons; parameter ID `arm`) |
| Dust | Max size (µm). The amount (Dust Density, particles per metre of groove) is set with the record tools in the GUI (it is visible to the host as a parameter and can be automated) |
| Scratch | Depth (µm), Pile-up (half-width of the raised material, mm; larger gives more low end, levelling off above about 1.5 mm). Both act on all scratches |
| Static Electricity | Charge |
| Output | Noise (cartridge output gain), Mechanics (airborne mechanical sound gain), Dry (original signal, Off at minimum) |

## Record tools

Pick a tool with the icons at the top left of the record (press the selected icon again to put it down).

- **Dust** Dust builds up while you hold the mouse on the record. It spreads over the whole surface wherever you press, reaching the maximum (60/m) after about 5 seconds of pressing in total. The amount is shown as the record turning from black to a mottled grey (the mottling is only visual; the sound uses evenly spread dust).
- **Nail** Press and release to cut a scratch through the point where you let go. Scratches are straight, tilted a little from the radial direction (random each time). The length depends on how long you held: about 2 mm at the shortest, the maximum (2/3 of the recorded band's width) after about 2 seconds. While you hold, the cut-to-be is previewed in orange. Releasing off the record cuts nothing. Up to 32 scratches; beyond that the oldest one disappears.
- **Eraser** Hover the eraser over the record and the scratch a click would remove is highlighted in orange: of the scratches that passed within about 12 px of the pointer during the last 0.4 s, the one that passed last. Even on a spinning record, clicking just after a scratch has passed under the pointer removes it (on a standing record, the nearest scratch). Clicking bare surface removes about 5% of the maximum dust, and holding (after 0.3 s) keeps removing it at the same rate as the dust tool adds it.

The tools work on the record's angle as drawn on screen, so you can cut and remove scratches where you see them, whether the record is turning or not.

Scratches are stored by their relative position within the recorded band, so they stay in the same place when you change the disc size. They are saved with the project state (not automatable).
Scratches made with the earlier Count / Length / Layout controls are taken over in the same layout when an old project is opened.

## Model

The computation runs groove shape → stylus tip geometry → stylus dynamics → MM cartridge → RIAA. The mechanical system is oversampled to about 350 kHz.

- **Groove speed** v = 2πr·rpm/60. It depends on 33 rpm (computed as 33⅓) / 45 rpm, the disc size and the stylus position (outer → inner), and sets the length and rate of the clicks.
- **Groove walls** Following the 45/45 system, the left and right walls are handled independently. The right channel is inverted, so lateral motion comes out as L+R and vertical motion as L−R.
- **Stylus** The tip mass is pressed against the walls through Hertzian contact (F = Kδ^1.5, pushing back on one side only). The cantilever spring (compliance 20 µm/mN) connects it to the tonearm (12 g, resonance about 10 Hz). The output is the tip's velocity relative to the arm.
- **Dust (crackle)** Occurs as a Poisson process per length of groove, with Pareto-distributed particle sizes. A particle usually sits on one wall; the path of the tip rolling over the sphere is applied as displacement.
- **Scratches (thump)** A scratch sits at a fixed angle on the disc and sounds once per revolution. The edges of the cut and the burr give the sharp "t" component. The low "b" component comes from the pile-up of material pushed aside by the cut (Pile-up, a few mm wide, 50–100% of the depth high). The pile-up lifts the stylus faster than the arm resonance (about 10 Hz) lets the arm follow, so it appears at the cartridge output, and RIAA boosts the lows further. Each scratch spans a range of radii, so it appears and disappears as the stylus moves on.
- **Static Electricity (fizz)** Friction from the rotation charges the record. Depending on the charge, an avalanche of discharges starts: small discharges follow densely until part of the charge is gone, then it stops and charges up again. The discharge pulses ring at the resonance of the cartridge inductance and load capacitance (about 16 kHz, Q = 3). A charged record also attracts dust.
- **Raising and lowering the arm** The arm-rest clamp is worked by hand before the START/STOP buttons are pressed. The lifter and the arm swing are driven by a cam that a single motor turns in one direction only.
  - Buttons: a fingertip (a soft spring and damper) pushes the key down. The tactile switch dome passes its force peak and buckles, and the key hits bottom with a click. When the finger lets go, the dome springs back and the key hits its upper stop. The key is a tongue moulded into the front panel.
  - Motor: a small DC motor driven at 1.5 V. Its electrical (resistance, inductance, back-EMF) and mechanical (inertia, gear friction) equations are solved. A worm gear (1:30) and spur gears (8:88) turn the cam. One cam revolution does one down and one up; half a turn takes about 3.2 s. Spin-up at start and run-down at stop both come out of these equations.
  - Cam: each half turn does "raise lifter → swing arm → lower lifter", the first half down, the second half up. The motor stops when the limit switch's leaf drops into a notch in the cam. A button bridges the switch to start the motor, which keeps running once the leaf has climbed out of the notch. If the opposite action is requested midway, the cam does not reverse but runs on to the next stop.
  - Lifter: the lifter rod rests on a spring-loaded follower lever. The lever is raised by a gentle ramp on the cam; for lowering it falls off a step, hits the cam and bounces (a knock). The rod leaves the lever and sinks slowly at the oil damper's pace (Cue damping). The damper piston only works inside its bore, though: from 0.6–1 mm above the record (slightly different each time) the rod falls freely. The arm drops this last distance under its own weight and the stylus hits the groove, giving a low, loud thump from the cartridge (the arm's resonance of about 10 Hz and the RIAA bass boost).
  - Arm swing: the cam swings the arm through a slipping clutch. On the way back the cam runs a little past the arm-rest stop, so the arm hits the stop at its speed just before it and bounces a few times, pushed by the clutch, before settling (a clack).
  - Down: open the clamp → press START → wait 1 s (for the platter to come up to speed) → motor runs (raise lifter → swing over the record → lever drops) → slow damped descent → falls the last millimetre or so and lands (thump)
  - Up: press STOP → motor runs (raise lifter → lift-off → arm hits the arm-rest stop → lever drops) → arm settles on the rest → close the clamp
  - The arm has its own weight (the tracking force) and is supported from below, one-sidedly, by the lifter's silicone pad or the arm rest's rubber seat. Touchdown (the arm's weight passing onto the stylus) and lift-off come straight out of these dynamics at the cartridge output.
  - The original (input) signal plays in proportion to the share of the tracking force carried by the groove. While the stylus is off the record it is silent, and so are dust, scratches and static.
- **Airborne mechanical sound (Mechanics)** The sound of the buttons, motors and gears, cam follower lever, limit switches, lifter and arm rest. It does not pass through the cartridge or RIAA.
  - Force sources
    - Impacts of the clamp on the arm or its rubber stop (Hertzian contact with damping)
    - Buttons: finger force, dome reaction, the key hitting bottom and its upper stop
    - Contact force between follower lever and cam (including the fine roughness of the cam surface). While it falls off the step only the spring force remains.
    - The limit switch leaf dropping into and climbing out of a notch (it lands softly on an elastomer tip)
    - The arm hitting the arm-rest stop (including the clutch's push)
    - Contact forces between arm and lifter or arm rest, computed by the engine
    - The motor's reaction torque (including commutator ripple and brush friction) and rotor unbalance
    - Tooth loads changing with each gear mesh; the moulded teeth each have their own error.
  - Radiating parts: aluminium arm tube, spring-steel clamp, steel posts, motor can, gearbox cover, button keys, front panel, limit switch leaf, POM follower lever, MDF plinth (15 mm)
    - Natural frequencies are computed from dimensions and materials using beam, plate and cylindrical shell theory. For plates, the mode shape at the drive point is taken into account (modes driven near a node barely sound).
    - The arm tube (headshell and cartridge, 15 g), the clamp's hook, the lifter bar and arm rest on top of their posts, the button caps and the lever's tip are included as tip masses.
  - Transmission: below their resonance the posts act as rigid, so contact forces pass straight into the plinth. The motors sit on rubber grommets, so vibration above the grommet resonance reaches the plinth only weakly.
  - Poor radiation by parts that are small compared with the wavelength (radiation efficiency) and the fall-off over the listening distance (0.7 m) are included.
  - The pitch and loudness balance comes from analysing video of a real deck (a belt-drive automatic). Dimensions, materials and speeds of the parts were chosen so that each sound's spectrum and relative level come close to it.
  - So that no two actions sound exactly alike, each action slightly changes where and how tightly the parts meet: each part's resonances vary by about ±35 cents and the drive strength by about ±2 dB. Impact speeds, the button dome's stiffness, the finger's speed and holding time, and the motor voltage (speed and gear pitch) also vary a little each time.

## Building

```
cmake -S . -B build                      # fetches JUCE 8.0.4 with FetchContent
cmake -S . -B build -DDUSTYTABLE_JUCE_PATH=<path to JUCE>   # to use a local JUCE
cmake --build build --config Release
```

Output: `build/DustyTable_artefacts/Release/VST3/DustyTable.vst3` (and the Standalone).

The Standalone's application icon is `Resources/icon_big.png` (1024 px) and `Resources/icon_small.png` (simplified for 16–48 px), embedded into the executable at build time. To change the design, redraw them with `python Tools/make_icon.py` (requires Pillow).

`DustyTableRender [output dir] [seconds]` renders each noise component to WAV and prints its levels (for checking and calibrating the physical model without a DAW).

`DustyTableUiCheck [output dir]` creates the editor offscreen, works the record tools step by step and writes a snapshot (PNG) of each step along with the dust amount and scratch count. It also checks saving and loading, importing old-format projects, and the stylus switch.

### Releases

Pushing a tag starting with `v` makes GitHub Actions (`.github/workflows/release.yml`) build the Windows version and, if `DustyTableUiCheck` passes, publish the zip as a GitHub Release. The tag must match `project(DustyTable VERSION x.y.z)` in `CMakeLists.txt` (otherwise it stops before building).

```
# after raising VERSION in CMakeLists.txt and committing
git tag v0.1.0
git push origin v0.1.0
```

Running the workflow by hand from the Actions tab builds the zip and stores it as an artifact without creating a release.

## License

The source code, documentation and icons in this repository are under the [MIT License](LICENSE).

The plugin and application, however, include [JUCE](https://juce.com), fetched at build time. JUCE 8 is dual-licensed under the AGPLv3 and a commercial licence (JUCE 8 EULA), and the VST3 SDK bundled with JUCE 8.0.4 is dual-licensed under the GPLv3 and Steinberg's proprietary licence. Binaries built with JUCE under its open source licence (`DustyTable.vst3`, `DustyTable.exe`, `DustyTableUiCheck`) must therefore be distributed under the terms of the AGPLv3 (if you distribute binaries, the corresponding source code must be made available).

The physical model (`Source/dsp`) and the renderer (`Tools/Render.cpp`, `DustyTableRender`) do not use JUCE and can be used under the MIT License alone.
