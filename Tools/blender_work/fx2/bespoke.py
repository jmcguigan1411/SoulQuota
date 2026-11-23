from gen import extra, r
# iteration 1: shared events only (the reapers use their template + motif)
extra("*", "Impact", r("Flash", col="Core", Up=40, Size=140, Life=0.13), r("Wave", "Teeth", "Mid", Up=8, Radius="1R", Life=0.28, Bright=1.0, Spin=70), r("Wave", col="Mid", Up=10, Radius="0.85R", Life=0.5, Delay=0.07, Bright=0.6),
      r("Mark", "Spikes", "Mid", Size="1.5R", Life=0.4, Bright=0.4), r("Cracks", col="Mid", Radius="0.7R", Count=7, Life=0.9), r("Burst", "Streak", "Core", Up=30, Angle=180, Count=12, Speed=800, Size=70, Life=0.24, Lift=0.25),
      r("Burst", "Shard", "Dark", Up=10, Angle=180, Count=6, Speed=380, Size=30, Life=0.6, Lift=1.0, Grav=-1100), r("Rise", "Ember", "Mid", Radius="0.7R", Count=6, Speed=120, Size=14, Life=0.8, Delay=0.1),
      r("Pillar", col="Core", Size=18, Lift="0.9R", Life=0.22))
# aura ticks and small repeating hits: three layers, no debris, no shake (fires many times a second)
extra("*", "ImpactLight", r("Wave", "Teeth", "Mid", Up=8, Radius="1R", Life=0.26, Bright=0.7, Spin=70), r("Flash", col="Core", Up=40, Size=70, Life=0.1),
      r("Burst", "Streak", "Core", Up=30, Angle=180, Count=5, Speed=600, Size=50, Life=0.2, Lift=0.2))
# boss slams and elite area hits: everything Impact has, taller and with a shake
extra("*", "ImpactHeavy", r("Flash", col="Core", Up=60, Size=240, Life=0.15), r("Wave", "Teeth", "Mid", Up=8, Radius="1R", Life=0.32, Bright=1.0, Spin=50), r("Wave", col="Mid", Up=10, Radius="0.85R", Life=0.6, Delay=0.08, Bright=0.6),
      r("Wave", col="Alt", Up=12, Radius="0.6R", Life=0.7, Delay=0.16, Bright=0.5), r("Cracks", col="Mid", Radius="0.85R", Count=11, Life=1.4),
      r("Burst", "Streak", "Core", Up=30, Angle=180, Count=18, Speed=1000, Size=90, Life=0.28, Lift=0.3), r("Burst", "Shard", "Dark", Up=10, Angle=180, Count=12, Speed=520, Size=44, Life=0.8, Lift=1.2, Grav=-1100),
      r("Pillar", col="Core", Size=34, Lift="1.3R", Life=0.3), r("Ring", "Flame", "Mid", Up=10, Radius="0.7R", Count=8, Size=70, Life=0.6, Lift=120), r("Dust", col="Dark", Radius="0.6R", Count=8, Size=80, Speed=260, Angle=180, Life=0.8),
      r("Shake", Size=0.6))
extra("*", "ShrinePulse", r("Wave", col="Mid", Up=8, Radius="1R", Life=0.7, Bright=0.6), r("Mark", "Sigil", "Mid", Size="1.2R", Life=0.9, Spin=40, Bright=0.5), r("Rise", "Star", "Core", Radius="0.5R", Count=6, Speed=180, Size=22, Life=0.9),
      r("Pillar", col="Core", Size=20, Lift=380, Life=0.5))
extra("*", "HazardPulse", r("Wave", "Teeth", "Mid", Up=8, Radius="1R", Life=0.4, Bright=0.8, Spin=-60), r("Ring", "Flame", "Mid", Up=10, Radius="0.8R", Count=6, Size=50, Life=0.5, Lift=90),
      r("Burst", "Ember", "Core", Up=20, Angle=180, Count=6, Speed=260, Size=12, Life=0.6, Lift=0.9, Grav=-400))
extra("*", "SoulCollect", r("Pop", "Wisp", "Core", Up=90, Size=130, Life=0.4, Lift=300, Bright=0.9), r("Ring", "Ember", "Mid", Up=70, Radius=45, Count=4, Size=22, Life=0.45, Spin=500, Lift=180),
      r("Pop", "Spikes", "Core", Up=85, Size=120, Life=0.13), r("Wave", "Teeth", "Mid", Up=6, Radius=70, Life=0.25, Bright=0.7, Spin=120))
extra("*", "GoldCollect", r("Pop", "Star", "Core", Up=85, Size=90, Life=0.22, Spin=300), r("Burst", "Ember", "Mid", Up=80, Angle=180, Count=4, Speed=220, Size=16, Life=0.35, Lift=0.9, Grav=-500),
      r("Pillar", col="Core", Size=8, Lift=160, Life=0.2))
extra("*", "Death", r("Dust", col="Mid", Up=40, Count=3, Size="1.1R", Speed=110, Angle=180, Life=0.45), r("Pop", "Wisp", "Core", Up=60, Size="2.6R", Life=0.5, Lift=220, Bright=0.9),
      r("Pop", "Spikes", "Core", Up=50, Size="2.4R", Life=0.12), r("Burst", "Streak", "Mid", Up=50, Angle=180, Count=4, Speed=420, Size=44, Life=0.2, Lift=0.5),
      r("Burst", "Ember", "Core", Up=50, Angle=180, Count=3, Speed=220, Size=16, Life=0.45, Lift=0.9))
extra("*", "LevelUp", r("Pillar", col="Core", Size=30, Lift=620, Life=0.6), r("Wave", "Teeth", "Mid", Up=8, Radius=260, Life=0.35, Spin=60), r("Wave", col="Mid", Up=10, Radius=380, Life=0.6, Delay=0.08, Bright=0.5),
      r("Ring", "Star", "Mid", Up=30, Radius=90, Count=8, Size=44, Life=1.0, Spin=260, Lift=170), r("Rise", "Ember", "Core", Radius=120, Count=14, Speed=260, Size=16, Life=0.9),
      r("Mark", "Sigil", "Mid", Size=340, Life=1.0, Spin=70, Bright=0.7), r("Flash", col="Core", Up=90, Size=150, Life=0.14), r("Shake", Size=0.2))
extra("*", "Collect", r("Pop", "Spikes", "Core", Up=80, Size=110, Life=0.14), r("Wave", col="Mid", Up=6, Radius=60, Life=0.22, Bright=0.7),
      r("Rise", "Ember", "Mid", Up=50, Radius=30, Count=3, Speed=260, Size=18, Life=0.4))
extra("*", "ChestOpen", r("Pillar", col="Core", at="T", Size=26, Lift=520, Life=0.6), r("Burst", "Star", "Mid", "T", Up=40, Angle=180, Count=12, Speed=420, Size=30, Life=0.8, Lift=1.3, Grav=-800),
      r("Burst", "Ember", "Core", "T", Up=40, Angle=180, Count=10, Speed=300, Size=14, Life=0.9, Lift=1.5, Grav=-500), r("Wave", col="Core", at="T", Up=8, Radius=220, Life=0.35),
      r("Mark", "Spikes", "Mid", "T", Size=300, Life=0.6, Spin=40, Bright=0.8), r("Flash", col="Core", at="T", Up=50, Size=140, Life=0.14), r("Shake", Size=0.25))
extra("*", "LanternLit", r("Pillar", col="Core", at="T", Size=40, Lift=900, Life=0.9), r("Wave", "Teeth", "Mid", "T", Up=8, Radius=600, Life=0.6, Spin=30), r("Wave", col="Mid", at="T", Up=10, Radius=450, Life=0.9, Delay=0.12, Bright=0.6),
      r("Ring", "Flame", "Mid", "T", Up=30, Radius=150, Count=8, Size=80, Life=1.4, Spin=120, Lift=120), r("Rise", "Wisp", "Alt", "T", Radius=260, Count=12, Speed=160, Size=40, Life=1.3),
      r("Mark", "Sigil", "Mid", "T", Size=700, Life=1.6, Spin=20, Bright=0.6), r("Flash", col="Core", at="T", Up=120, Size=220, Life=0.16), r("Shake", Size=0.3))
extra("*", "LanternSpent", r("Wave", "Teeth", "Mid", "T", Up=8, Radius=420, Life=0.5, Bright=0.7, Spin=-40), r("Burst", "Ember", "Core", "T", Up=140, Angle=180, Count=18, Speed=460, Size=20, Life=1.0, Lift=1.2, Grav=-500),
      r("Dust", col="Dark", at="T", Up=80, Count=7, Size=80, Speed=170, Angle=180, Life=1.0), r("Rise", "Wisp", "Alt", "T", Radius=120, Count=6, Speed=240, Size=50, Life=1.0),
      r("Pop", "Spikes", "Core", "T", Up=140, Size=220, Life=0.14), r("Ring", "Flame", "Mid", "T", Up=20, Radius=120, Count=6, Size=60, Size2=0, Life=0.7, Lift=-20))
extra("*", "Bank", r("Rise", "Wisp", "Mid", "T", Up=40, Radius=70, Count=5, Speed=320, Size=60, Life=0.6), r("Pop", "Spikes", "Core", "T", Up=150, Size=150, Life=0.14),
      r("Pillar", col="Core", at="T", Size=16, Lift=420, Life=0.35), r("Wave", "Teeth", "Mid", "T", Up=8, Radius=140, Life=0.3, Bright=0.8, Spin=90))
extra("*", "Dash", r("Dust", col="Dark", Count=3, Size=40, Speed=160, Angle=40, Life=0.45), r("Burst", "Streak", "Core", Up=70, Angle=12, Count=6, Speed=700, Size=90, Life=0.2),
      r("Swoosh", col="Mid", Up=50, Radius=90, Angle=-80, Sweep=160, Size=0.8, Life=0.22, Bright=0.7))
extra("*", "Jump", r("Dust", col="Dark", Count=3, Radius=20, Size=34, Speed=120, Angle=180, Life=0.4), r("Wave", col="Mid", Up=6, Radius=70, Life=0.25, Bright=0.5))
extra("*", "Land", r("Dust", col="Dark", Count=5, Radius=20, Size=40, Speed=220, Angle=180, Life=0.5), r("Wave", col="Mid", Up=6, Radius=110, Life=0.3, Bright=0.6),
      r("Burst", "Streak", "Core", Up=14, Angle=180, Count=5, Speed=300, Size=36, Life=0.2, Lift=0.1))
