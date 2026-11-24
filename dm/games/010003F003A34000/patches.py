# PEANUTS: exact-build Pikachu overworld 60 FPS timing observations.
# Original instructions are preserved by the offline rewriter.
# Menus, battle and capture retain native 30 FPS; see README.md.
OBS = {
    '1c375692de4f4b879abe88742dc0ca60': [
        (0x7db078, 35 | (7 << 8), 'b945fd14', 'PEANUTS: opt-in sync-camera duration and phase correction'),
        (0x665330, 35 | (8 << 8), 'fc1d0fea', 'PEANUTS: preserve UI animation cadence'),
        (0x2d2a14, 35 | (12 << 8), 'b9007a68', 'PEANUTS: preserve field actor counter cadence'),
        (0x7da2b8, 35 | (13 << 8), '4e21d400', 'PEANUTS: opt-in scene test, projectile gravity and timestep'),
        (0x7da2c8, 35 | (14 << 8), '3d806280', 'PEANUTS: opt-in scene test, flight elapsed-time divisor'),
        (0x5ed1c0, 35 | (15 << 8), '1e210808', 'PEANUTS: opt-in scene test, model elapsed seconds'),
        (0x81fc50, 35 | (17 << 8), 'bd466e61', 'PEANUTS: opt-in scene test, shrinking capture ring phase'),
        (0x2d2dec, 35 | (18 << 8), '7100791f', 'PEANUTS: prevent a held frame from repeating its random event'),
        (0x2d2e30, 35 | (19 << 8), 'b9414a69', 'PEANUTS: restore the original counter before its timeout comparison'),
        (0x7d9034, 35 | (20 << 8), '6b0a013f', 'PEANUTS: opt-in capture hit-pause duration before inclusive comparison'),
        (0x7dcad0, 35 | (21 << 8), '8b285289', 'PEANUTS: identify the active capture event manager'),
        (0x210684, 35 | (22 << 8), '11000508', 'PEANUTS: preserve capture event script cadence after flight'),
        (0x808024, 35 | (23 << 8), '1e202020', 'PEANUTS: scale the loaded dodge arc frame duration at 60 Hz'),
        (0x7da2d0, 35 | (24 << 8), '6e21dc00', 'PEANUTS: preserve the original vertical flight path at double cadence'),
        (0x91d71c, 35 | (25 << 8), '4e040d01', 'PEANUTS: scale the pre-impact ball integration timestep'),
        (0x91d73c, 35 | (26 << 8), '6e21dc00', 'PEANUTS: keep the pre-impact ball trajectory at original sample positions'),
        (0x91f114, 35 | (25 << 8), '4e040d03', 'PEANUTS: scale the ground ball displacement timestep'),
        (0x91f068, 35 | (27 << 8), '1e201880', 'PEANUTS: preserve the ground ball interpolation duration'),
        (0x2f8b80, 35 | (1 << 8), 'd10183ff', 'Overworld update heartbeat'),
        (0x28bb30, 35 | (2 << 8), 'd10103ff', 'Battle entry: retain native cadence'),
        (0x28ef40, 35 | (2 << 8), 'd10143ff', 'Capture entry: retain native cadence'),
        (0x39610, 35 | (3 << 8), 'f862d922', 'Duration table load: select G0 for world step, retaining timer slot'),
        (0x21b6f0, 35 | (4 << 8), 'd10483ff', 'Movement entry: save and halve per-update offsets'),
        (0x21c368, 35 | (5 << 8), '910483ff', 'Sole movement epilogue: restore offsets'),
        (0x21bd08, 35 | (6 << 8), 'aa1503e0', 'Turning argument: preserve per-second interpolation'),
    ],
}

BINDING = [
    {'what': 'Scene-aware overworld 60 FPS, normal movement and turning speed',
     'now': 'build', 'goal': 'game', 'purpose': 'frame rate',
     'why': 'Game Freak fixed frame-duration and per-update movement semantics require matching code sites',
     'checked': 'Seven exact-build guards and control flow checked statically; device validation pending'},
]
