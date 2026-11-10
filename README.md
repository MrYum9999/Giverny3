# Giverny 3

An ambient reverb, delay, tape echo and phaser for guitar and synths, with a "drift" engine that keeps rearranging them. Painted in the spirit of Monet's water lilies.

## Controls

**The four effects** (each knob sets how much of that effect you hear)

| Knob | What it does |
|---|---|
| **Reverb** | A large, slowly shimmering room. Turning it up also makes the tail longer (about 1.5 to 11 seconds). |
| **Delay** | Clean stereo echoes that bounce left and right. |
| **Tape** | A worn tape echo with wow, flutter and saturation. Runs at 1.5x the delay time, so it weaves rhythmically with the clean delay. |
| **Phase** | A slow six-stage phaser. One sweep lasts 16 delay repeats, so it follows the tempo. |

**Blend** (centre)

At zero the effects run in a fixed order (Phase, Tape, Delay, Reverb) at exactly your knob settings. As you turn it up, the drift engine wakes up. Every few seconds it picks a new random order and new random intensities for each effect. Sometimes it lets an effect dip, sometimes it pushes one past its knob setting, but it never lets the effects fade out: the total amount always stays close to what your knobs are set to. Then it slowly crossfades to the new arrangement. Higher Blend means more frequent, more dramatic and faster changes.

Two full effect chains run side by side, so changing the order never cuts off a reverb or delay tail. During each crossfade only the effect is blended between the two arrangements; your dry signal stays at a steady level, so it never pokes through. The pond display at the top shows the current order as lilies floating from **in** to **out**. They slide past each other when the order changes, and their flowers open wider the stronger each effect is.

**Colour of the light**

| Knob | What it does |
|---|---|
| **Dark** | Darkens the whole sound with a smooth low-pass, and makes the reverb and echoes lose their highs faster. |
| **Light** | Brightens with a high shelf, and makes the reverb more reflective and glassy (denser diffusion, slower high-frequency decay). |
| **Distance** | Low = the sound sits close to you: present, clear, with a gap before the room arrives. Turning it up pushes everything further away (softer highs, more room) and brings in **distant waves**: blurred, darker echoes of the effect returning from up to four seconds away, each swelling and receding at its own slow rate and spread across the stereo field. |

**Time and memory**

| Control | What it does |
|---|---|
| **Time** | Delay time from 40 ms to 2 seconds. |
| **Sync** | Locks the delay to your DAW's tempo. Time then picks a note value (1/32 up to a whole note, including dotted and triplet). The BPM shows under the button. |
| **Repeats** | How many echoes the delay and tape give, from a single repeat to long, slowly fading trails. |

## Level safety

Long echoes and reverb tails can stack up a lot of energy on sustained notes. Giverny keeps this in check in four ways:
- the echo loops in the delay, tape and distant waves turn themselves down smoothly if they start to build up
- long reverb tails are scaled so they never get louder than the sound going into them
- a gentle compressor rides the whole effect so it never swells far above your playing
- an output limiter keeps the plugin at or below -3 dBFS, so two copies on separate tracks still leave the master bus room

## Building and installing

Giverny 3 has its own name and plugin ID, so it installs alongside earlier versions of Giverny.

1. Create a new GitHub repository (for example `Giverny3`).
2. Upload `CMakeLists.txt`, `README.md` and the `Source` folder.
3. Add a file at `.github/workflows/build.yml` with the contents of `build.yml` from this folder.
4. Wait for the green check on the Actions tab, then download **Giverny3-VST3** from Artifacts.
5. Extract it and copy the `Giverny3.vst3` folder into `C:\Program Files\Common Files\VST3`.

Built with [JUCE](https://juce.com/) 9.0.5.
