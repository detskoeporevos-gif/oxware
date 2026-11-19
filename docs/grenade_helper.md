# Grenade helper (accurate trajectory + saved throws)

Source module: `src/cheat/features/GrenadeHelper.cpp/.h`.

## What changed

- The arc is now **engine-accurate for CS 1.6 / build 4554**. It uses the same
  step order and constants as the well-known HL1 grenade predictor:
  per-weapon gravity/friction (HE `0.55/0.7`, flash & smoke `0.5/0.8`),
  pitch bias, 16-unit spawn offset, `(90 - pitch) * 6` launch speed,
  `PM_PlayerTrace` with hull 2, and fixed `0.01 s` steps.
- When you record a throw, its **landing point** is saved alongside angles.
- Replaying a throw **solves the angles for that landing point** from your
  current position, so F1/UI replay lands at the same spot even if you stand
  somewhere else. Camera stays still when `nade_silent 1`.
- Old records without a landing point fall back to the original angles.

## Commands / UI

```cfg
nade_record <name>     // arm recording; next grenade throw is saved
nade_throw  <name>     // replay toward the saved landing spot
nade_list              // list throws for the current map
nade_del    <name>     // delete a throw
```

Menu: **Grenades → Grenade helper** — name field, Record/Throw buttons,
list of saved throws with per-row Throw/Delete, plus all tuning sliders.

```cfg
nade_preview 1
nade_silent 1
nade_throw_speed 675   // 675 = exact engine value
nade_gravity_scale 1.0 // 1 = exact
nade_bounce 1.0        // 1 = exact engine response
nade_friction 1.0      // 1 = exact engine response
nade_preview_time 3.0
nade_throw_tolerance 64
```

Storage: `%APPDATA%\oxware\nades\<map>.json`.

## Note

The solver runs a bounded two-stage search (coarse then fine) over pitch/yaw
once when you trigger the throw. If no solution within 512 units is found, the
stored angles are used as a fallback. Bindings like `bind f1 "nade_throw flash_a"`
work as before.
