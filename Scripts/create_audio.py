"""
Soul Quota - synthesises placeholder sound effects + a music loop (pure Python, no assets needed)
and imports them as /Game/Audio/SFX_<Name>. The C++ plays them automatically once they exist.
Run in the editor (Tools > Execute Python Script) - setup_project.py runs it for you. Safe to re-run.
Replace any of them later by importing a real sound with the same name.
"""
import math, os, random, struct, wave
import unreal

RATE = 22050
random.seed(7)


def env(t, attack, decay_len):
    if t < attack:
        return t / attack
    return max(0.0, 1.0 - (t - attack) / decay_len)


def write(name, samples):
    folder = os.path.join(unreal.Paths.project_saved_dir(), "GeneratedAudio")
    os.makedirs(folder, exist_ok=True)
    path = os.path.join(folder, "SFX_" + name + ".wav")
    peak = max(0.001, max(abs(s) for s in samples))
    scale = 0.85 / peak
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s * scale)) * 32767)) for s in samples))
    return path


def tone(freq_fn, length, amp_fn, noise=0.0, lowpass=1.0):
    out, phase, lp = [], 0.0, 0.0
    for i in range(int(length * RATE)):
        t = i / RATE
        phase += 2 * math.pi * freq_fn(t) / RATE
        s = math.sin(phase) + noise * (random.random() * 2 - 1)
        lp += (s - lp) * lowpass
        out.append(lp * amp_fn(t))
    return out


def bell(fundamental, length, partials=(1.0, 2.0, 2.4, 3.0, 4.2), decay=1.2):
    out = []
    for i in range(int(length * RATE)):
        t = i / RATE
        s = sum(math.sin(2 * math.pi * fundamental * p * t) * math.exp(-t * decay * (1 + k * 0.6)) / (k + 1)
                for k, p in enumerate(partials))
        out.append(s)
    return out


def notes(seq, note_len, decay=6.0, wave_fn=math.sin):
    out = []
    for f in seq:
        for i in range(int(note_len * RATE)):
            t = i / RATE
            out.append(wave_fn(2 * math.pi * f * t) * math.exp(-t * decay))
    return out


SOUNDS = {
    "Squelch": lambda: tone(lambda t: 320 - 1600 * t, 0.14, lambda t: env(t, 0.005, 0.13), noise=0.9, lowpass=0.25),
    "Hit": lambda: tone(lambda t: 130 - 300 * t, 0.2, lambda t: env(t, 0.003, 0.19), noise=0.5, lowpass=0.4),
    "Soul": lambda: tone(lambda t: 1200 + 6000 * t, 0.09, lambda t: env(t, 0.004, 0.085)),
    "Bank": lambda: [a + b for a, b in zip(bell(880, 0.8, (1.0, 1.5, 2.0), 4.0), bell(1320, 0.8, (1.0, 2.0), 5.0))],
    "LevelUp": lambda: notes([523, 659, 784, 1047], 0.13, 8.0),
    "Bell": lambda: bell(110, 3.5, decay=0.9),
    "NightStart": lambda: [a * 0.7 for a in bell(147, 1.6, (1.0, 1.5, 2.0), 1.8)],
    "Slam": lambda: tone(lambda t: 60 - 40 * t, 0.7, lambda t: env(t, 0.004, 0.69), noise=0.7, lowpass=0.15),
    "Dash": lambda: tone(lambda t: 400 + 1200 * t, 0.25, lambda t: math.sin(math.pi * t / 0.25), noise=1.0, lowpass=0.08),
    "Victory": lambda: notes([392, 494, 587, 784, 988], 0.22, 4.0),
    "Defeat": lambda: notes([392, 349, 311, 262], 0.35, 3.0),
}


def music_loop():
    # 16 bars of spooky swing in A minor at 120 bpm: walking bass + plucked minor arpeggio + low drone
    bpm, beats = 120.0, 32
    beat = 60.0 / bpm
    total = int(beats * beat * RATE)
    out = [0.0] * total
    bass = [110, 131, 147, 165, 110, 98, 104, 110]
    arp = [440, 523, 659, 523, 440, 392, 415, 494]
    for b in range(beats):
        start = int(b * beat * RATE)
        f_bass = bass[b % len(bass)]
        for i in range(int(beat * RATE)):
            t = i / RATE
            if start + i < total:
                out[start + i] += 0.55 * math.sin(2 * math.pi * f_bass * t) * math.exp(-t * 3.0)
        if b % 2 == 1:  # swung off-beat pluck
            off = start + int(beat * 0.66 * RATE)
            f = arp[(b // 2) % len(arp)]
            for i in range(int(beat * RATE)):
                t = i / RATE
                if off + i < total:
                    out[off + i] += 0.25 * (math.sin(2 * math.pi * f * t) + 0.3 * math.sin(4 * math.pi * f * t)) * math.exp(-t * 7.0)
    for i in range(total):  # drone with slow tremolo (whole cycles so it loops cleanly)
        t = i / RATE
        out[i] += 0.12 * math.sin(2 * math.pi * 55 * t) * (0.7 + 0.3 * math.sin(2 * math.pi * t / (beats * beat)))
    return out


def import_wav(path, name):
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = "/Game/Audio"
    task.destination_name = "SFX_" + name
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])


for sound_name, make in SOUNDS.items():
    if unreal.EditorAssetLibrary.does_asset_exist("/Game/Audio/SFX_" + sound_name):
        continue
    import_wav(write(sound_name, make()), sound_name)

if not unreal.EditorAssetLibrary.does_asset_exist("/Game/Audio/SFX_Music"):
    import_wav(write("Music", music_loop()), "Music")
    music = unreal.load_asset("/Game/Audio/SFX_Music")
    if music:
        music.set_editor_property("looping", True)
        unreal.EditorAssetLibrary.save_asset("/Game/Audio/SFX_Music")

unreal.log("Soul Quota audio ready in /Game/Audio")
