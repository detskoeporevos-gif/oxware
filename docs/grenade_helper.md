# Grenade helper (trajectory + saved throws)

Source-only feature in `src/cheat/features/GrenadeHelper.cpp/.h`.

## What it does

1. **Trajectory preview** — while holding a grenade, an approximate flight arc and landing point are drawn over the world.
2. **Record a throw** — `nade_record <name>` arms recording; the next grenade throw saves:
   player origin, eye position, velocity, view angles, weapon id, buttons and msec, plus the current map and engine build.
3. **Replay a throw** — `nade_throw <name>` replays those angles/weapon/buttons through `usercmd`. The camera is restored in `V_CalcRefDef`, so the screen does not visibly turn.
4. **Per-map storage** — throws are saved to `%APPDATA%\oxware\nades\<map>.json`.

## Commands and variables

```cfg
nade_record <name>     // arm recording; next grenade throw is saved
nade_throw  <name>     // replay the saved throw
nade_list             // list throws for the current map
nade_del    <name>     // delete a throw
```

```cfg
nade_preview 1         // draw trajectory preview (default 1)
nade_silent 1          // keep camera still during replay (default 1)
nade_throw_speed 675   // initial grenade speed for the simulation
nade_gravity_scale 1.0
nade_bounce 0.45
nade_friction 0.70
nade_preview_time 3.0
nade_throw_tolerance 64   // max distance from recorded spot for replay
```

Saved throws are also marked in the world when `nade_preview 1`.

## Build 4554

No new hardcoded offsets are used: origin, velocity, viewangles and gravity come from the existing `CLocalState` / `CMemoryHookMgr` interfaces. The build number is stored with each throw; replaying a throw recorded on another build prints a warning.

The simulation is deliberately approximate (flat ground, tunable bounce/friction), so it is a visual aid, not a server-accurate predictor. The replay itself only writes networked `usercmd` fields.
