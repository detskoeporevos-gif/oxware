# Autonomous Movement Bot — build & test guide

## What was added

`CMovementBot` (`src/cheat/features/movement/MovementBot.cpp/.h`) is an
autonomous controller. When enabled it owns the whole `usercmd_t` for the
frame: it picks a destination, points view yaw at it, walks/jumps, and uses an
air-strafe controller in the air. Other movement features are skipped while
the bot is active, so they cannot fight it for buttons.

It only writes networked input (`viewangles`, `forwardmove`, `sidemove`,
`buttons`). It does **not** patch `pmove->origin/velocity/fuser*`.

Two modes:

1. **Free roam** (no waypoints) — the bot generates a point 64–1024 units in
   front of itself, re-picks it when reached, when stuck for 1.5 s, or after
   12 s.
2. **Waypoint route** — record points manually; the bot loops through them.

Also changed: `CLocalState::is_on_ground_safe()` now prefers `pmove->onground`
over the one-frame-lagged `clientdata.flags`.

## Build

The project is MSVC/Windows-only (`v143`, C++20). Build it in Visual Studio
2022 on Windows:

1. Open `oxware.sln`.
2. Select **Release** (or **Retail**) and **Win32**.
3. `Build > Rebuild Solution`.
4. Output: `build\cheater.dll` + `build\oxware.exe`.

There is also a GitHub Actions workflow: `.github/workflows/build.yml`. It
builds all static libraries in dependency order, then `cheater.vcxproj`, and
uploads `build/**` as the `oxware-release-x86` artifact. Trigger it with a push
to `arena/**` or via `workflow_dispatch`.

The Linux sandbox used for this patch has no MSVC/`windows.h`, so the build
was executed on a `windows-2022` GitHub runner instead. That CI run completed
successfully; the `oxware-release-x86` artifact contains the built output.

Additionally verified locally:

- both modified `.vcxproj` files are valid XML and include the new files;
- the standalone math harness for the steering/air-strafe formula compiled
  with `g++ -std=c++20` and passed all checks.

CI required two compatibility fixes for current VS2022 toolchains:

- PHNT's private structs that collide with modern Windows SDK headers are now
  guarded by SDK-version checks;
- all custom `std::formatter::format()` members are `const`, as required by
  MSVC 17.14's `<format>`.

## Enable / disable

In the cheat console (`~`):

```cfg
// persistent autonomous mode (created automatically for every InCommand)
movement_bot_always_enabled 1

// ...or bind a key in the game console / UI key binder to "movement_bot"
// bind x movement_bot      // hold-to-enable
```

Disable:

```cfg
movement_bot_always_enabled 0
movement_bot_enable 0
```

The feature also appears in the feature list as **Movement Bot**.

## Recording a route

Bind these two commands to keys (game console or UI key binder):

```cfg
bind f1 movement_bot_waypoint       // press once = record current origin
bind f2 movement_bot_route_clear    // press once = clear route -> free roam
```

Turn on logging while recording:

```cfg
movement_bot_debug 1
```

## Tuning

```cfg
movement_bot_reach_dist 48      // waypoint considered reached within N units
movement_bot_roam_distance 320  // free-roam target distance
movement_bot_use_bhop 1         // auto-jump on grounded frame
movement_bot_use_strafe 1       // air-strafe toward target
movement_bot_max_speed 0        // 0 = use pmove->maxspeed; e.g. 250 to cap
movement_bot_debug 0
```

## Quick test

1. Start CS 1.6 (build 8684/4554/3266 supported by this repo).
2. Inject with `oxware.exe`, open the menu with `INSERT`.
3. Join a **local/offline server** first.
4. Open the cheat console (`~`) and run:
   `movement_bot_always_enabled 1`, `movement_bot_debug 1`.
5. The player should start moving on its own, jumping on every grounded
   frame and steering smoothly in the air.
6. Run `cl_showpos 1` to watch velocity. Enabling `movement_plot 1` shows the
   jump/duck/velocity graphs used by the movement features.
7. Press F1 a few times while walking to record a route; the bot will loop it.
8. Press F2 to clear the route and return to free roam.

## Troubleshooting

- **Bot does nothing**: it requires `connected + alive + not in chat`.
  `movement_bot_always_enabled` must be `1`, or the bound key held.
- **Spins in place**: increase `movement_bot_reach_dist`, decrease
  `movement_bot_roam_distance`, or record a short explicit route.
- **Server corrects/warps you**: set `movement_bot_max_speed 250` and disable
  any msec/speed hacks (`movement_bhop_mode_noslowdown 0`).
- **Other movement features interfere**: they should not — `CMovement`
  returns early while the bot is enabled. Disable them anyway while testing.
- **Stuck against a wall**: the bot re-picks the goal after ~1.5 s of no
  horizontal movement; if the route repeats a bad point, clear it with F2.
