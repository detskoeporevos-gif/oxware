/*
*	OXWARE developed by oxiKKK
*	Copyright (c) 2023
*
*	This program is licensed under the MIT license. By downloading, copying,
*	installing or using this software you agree to this license.
*/

//
// GrenadeHelper.cpp -- grenade trajectory preview and saved-throw replay.
//
// The whole thing is built on already available interfaces:
//   - CLocalState / CMemoryHookMgr for origin, velocity, viewangles and gravity;
//   - CWeapons for the current weapon id;
//   - the existing ImGui background rendering context for the arc.
//
// No new hardcoded offsets are needed, so the feature follows whatever build
// of the game the rest of the cheat has already detected.
//

#include "precompiled.h"

#include <filesystem>
#include <fstream>
#include <cctype>
#include <cfloat>

//-----------------------------------------------------------------------------

VarBoolean nade_preview("nade_preview", "Preview the trajectory of the current grenade", true);
VarBoolean nade_silent("nade_silent", "Keep the camera still while replaying a saved throw", true);
VarFloat nade_throw_speed("nade_throw_speed", "Initial grenade speed", 675.0f, 100.0f, 1500.0f);
VarFloat nade_gravity_scale("nade_gravity_scale", "Gravity multiplier used by the trajectory simulation", 1.0f, 0.1f, 5.0f);
VarFloat nade_bounce("nade_bounce", "Extra multiplier on the exact bounce response (1 = engine-accurate)", 1.0f, 0.0f, 2.0f);
VarFloat nade_friction("nade_friction", "Extra multiplier on the engine friction (1 = engine-accurate)", 1.0f, 0.0f, 2.0f);
VarFloat nade_preview_time("nade_preview_time", "How many seconds of flight to simulate", 3.0f, 0.5f, 8.0f);
VarFloat nade_throw_tolerance("nade_throw_tolerance", "Max distance from the recorded spot that still allows the throw", 64.0f, 0.0f, 512.0f);

BaseCommand nade_record_cmd(
	"nade_record", "<name>", "Arm recording. The next grenade throw is saved under <name>.",
	[](BaseCommand* cmd, const CmdArgs& args)
	{
		if (args.count() != 2)
		{
			cmd->print_usage();
			return;
		}
		if (CGrenadeHelper::the().arm_record(args.get(1)))
		{
			CConsole::the().info("Recording armed as '{}'. Throw a grenade to save it.", args.get(1));
		}
	}
);

BaseCommand nade_throw_cmd(
	"nade_throw", "<name>", "Replay a previously saved grenade throw.",
	[](BaseCommand* cmd, const CmdArgs& args)
	{
		if (args.count() != 2)
		{
			cmd->print_usage();
			return;
		}
		CGrenadeHelper::the().throw_nade(args.get(1));
	}
);

BaseCommand nade_list_cmd(
	"nade_list", "List saved throws for the current map.",
	[](BaseCommand* cmd, const CmdArgs& args)
	{
		CGrenadeHelper::the().list_nades();
	}
);

BaseCommand nade_del_cmd(
	"nade_del", "<name>", "Delete a saved grenade throw.",
	[](BaseCommand* cmd, const CmdArgs& args)
	{
		if (args.count() != 2)
		{
			cmd->print_usage();
			return;
		}
		if (CGrenadeHelper::the().delete_nade(args.get(1)))
		{
			CConsole::the().info("Deleted throw '{}'.", args.get(1));
		}
	}
);

//-----------------------------------------------------------------------------

static std::string safe_filename(const std::string& input)
{
	std::string out;
	out.reserve(input.size());
	for (unsigned char c : input)
	{
		if (std::isalnum(c) || c == '_' || c == '-')
		{
			out.push_back(static_cast<char>(c));
		}
		else
		{
			out.push_back('_');
		}
	}
	return out.empty() ? "unknown" : out;
}

void CGrenadeHelper::initialize_gui()
{
	COxWareUI::the().add_background_rendering_code([this]() { on_render(); });
	CConsole::the().info("GrenadeHelper initialized (build {}, trajectory + saved throws).",
						COxWare::the().get_build_number());
}

std::filesystem::path CGrenadeHelper::storage_path() const
{
	std::filesystem::path dir = g_filesystem_i->get_appdata_path() / "nades";
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	return dir / (safe_filename(m_current_map.empty() ? "unknown" : m_current_map) + ".json");
}

void CGrenadeHelper::save()
{
	std::error_code ec;
	auto path = storage_path();

	nh::json j = nh::json::array();
	for (const auto& [name, t] : m_throws)
	{
		nh::json entry;
		entry["name"] = t.name;
		entry["map"] = t.map;
		entry["build"] = t.build;
		entry["weapon_id"] = t.weapon_id;
		entry["origin"] = { t.origin.x, t.origin.y, t.origin.z };
		entry["eye"] = { t.eye.x, t.eye.y, t.eye.z };
		entry["velocity"] = { t.velocity.x, t.velocity.y, t.velocity.z };
		entry["angles"] = { t.angles.x, t.angles.y, t.angles.z };
		entry["landing"] = { t.landing.x, t.landing.y, t.landing.z };
		entry["buttons"] = t.buttons;
		entry["msec"] = t.msec;
		j.push_back(entry);
	}

	std::ofstream out(path, std::ios::trunc);
	if (!out.good())
	{
		CConsole::the().error("GrenadeHelper: couldn't write '{}'.", path.string());
		return;
	}
	out << j.dump(4);
}

void CGrenadeHelper::load()
{
	auto path = storage_path();
	m_throws.clear();

	std::error_code ec;
	if (!std::filesystem::exists(path, ec))
	{
		return;
	}

	std::ifstream in(path);
	if (!in.good())
	{
		return;
	}

	nh::json j;
	try
	{
		in >> j;
	}
	catch (const std::exception& e)
	{
		CConsole::the().error("GrenadeHelper: couldn't parse '{}': {}", path.string(), e.what());
		return;
	}

	if (!j.is_array())
	{
		return;
	}

	auto read_vec = [](const nh::json& value) -> Vector
	{
		if (!value.is_array() || value.size() < 3)
		{
			return Vector(0.0f, 0.0f, 0.0f);
		}
		return Vector(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
	};

	for (const auto& entry : j)
	{
		if (!entry.contains("name"))
		{
			continue;
		}

		grenade_throw_t t;
		t.name = entry.value("name", std::string());
		if (t.name.empty())
		{
			continue;
		}
		t.map = entry.value("map", std::string());
		t.build = entry.value("build", 0);
		t.weapon_id = entry.value("weapon_id", 0);
		t.origin = read_vec(entry.value("origin", nh::json::array()));
		t.eye = read_vec(entry.value("eye", nh::json::array()));
		t.velocity = read_vec(entry.value("velocity", nh::json::array()));
		t.angles = read_vec(entry.value("angles", nh::json::array()));
		t.landing = read_vec(entry.value("landing", nh::json::array()));
		t.buttons = static_cast<unsigned short>(entry.value("buttons", 0));
		t.msec = static_cast<unsigned char>(entry.value("msec", 0));

		m_throws[t.name] = t;
	}
}

void CGrenadeHelper::reload_if_needed()
{
	auto cl = CMemoryHookMgr::the().cl().get();
	std::string map = cl && cl->levelname[0] ? std::string(cl->levelname) : std::string();
	if (map == m_current_map)
	{
		return;
	}

	m_current_map = map;
	m_pending_record.clear();
	m_active_throw.clear();
	m_replay_frames = 0;
	m_silent_frames = 0;
	load();
}

//-----------------------------------------------------------------------------

bool CGrenadeHelper::arm_record(const std::string& name)
{
	reload_if_needed();
	if (name.empty())
	{
		return false;
	}
	m_pending_record = name;
	return true;
}

bool CGrenadeHelper::throw_nade(const std::string& name)
{
	reload_if_needed();

	auto it = m_throws.find(name);
	if (it == m_throws.end())
	{
		CConsole::the().error("GrenadeHelper: no throw named '{}' for map '{}'.", name, m_current_map);
		return false;
	}

	const auto& t = it->second;
	if (t.build != 0 && t.build != COxWare::the().get_build_number())
	{
		CConsole::the().warning("GrenadeHelper: throw '{}' was recorded on build {}, current build is {}.",
							   name, t.build, COxWare::the().get_build_number());
	}

	auto local = CLocalState::the().local_player();
	if (!local)
	{
		CConsole::the().error("GrenadeHelper: can't throw while dead/spectating.");
		return false;
	}

	float dist = CLocalState::the().get_origin().Distance(t.origin);
	if (dist > nade_throw_tolerance.get_value())
	{
		CConsole::the().error("GrenadeHelper: too far from the recorded spot ({:.1f} > {:.1f}).",
							 dist, nade_throw_tolerance.get_value());
		return false;
	}

	auto cl = CMemoryHookMgr::the().cl().get();
	m_saved_camera_angles = cl ? cl->viewangles : Vector(0.0f, 0.0f, 0.0f);

	// aim at the recorded landing spot, not at the recorded view direction.
	// solving once avoids running hundreds of traces inside the replay frames.
	if (!t.landing.IsZero())
	{
		m_replay_angles = solve_angles_for_landing(t.landing, t.weapon_id);
	}
	if (m_replay_angles.IsZero())
	{
		m_replay_angles = t.angles;
	}

	m_original_weapon_id = CWeapons::the().get_current_weapon()->m_weapon_id;
	m_active_throw = name;
	// two frames: release attack + select the weapon, then throw. This gives
	// the engine a clean 0 -> 1 transition even if the player holds fire.
	m_replay_frames = 2;
	m_silent_frames = nade_silent.get_value() ? 4 : 0;

	CConsole::the().info("GrenadeHelper: throwing '{}'.", name);
	return true;
}

void CGrenadeHelper::list_nades() const
{
	if (m_throws.empty())
	{
		CConsole::the().info("GrenadeHelper: no saved throws for '{}'.", m_current_map);
		return;
	}

	CConsole::the().info("GrenadeHelper: saved throws for '{}':", m_current_map);
	for (const auto& [name, t] : m_throws)
	{
		CConsole::the().info("  {} (weapon {}, build {})", name, t.weapon_id, t.build);
	}
}

bool CGrenadeHelper::delete_nade(const std::string& name)
{
	reload_if_needed();
	auto it = m_throws.find(name);
	if (it == m_throws.end())
	{
		return false;
	}
	m_throws.erase(it);
	save();
	return true;
}

std::vector<std::string> CGrenadeHelper::throw_names()
{
	reload_if_needed();
	std::vector<std::string> names;
	names.reserve(m_throws.size());
	for (const auto& [name, t] : m_throws)
	{
		names.push_back(name);
	}
	return names;
}

//-----------------------------------------------------------------------------

void CGrenadeHelper::update_clientmove(float frametime, hl::usercmd_t* cmd)
{
	reload_if_needed();

	auto cl = CMemoryHookMgr::the().cl().get();
	if (!cl || !cmd)
	{
		return;
	}

	const bool attack = (cmd->buttons & IN_ATTACK) != 0;
	auto weapon = CWeapons::the().get_current_weapon();

	if (!m_pending_record.empty() && attack && !m_prev_attack && weapon && weapon->is_nade())
	{
		auto local = CLocalState::the().local_player();
		if (local)
		{
			grenade_throw_t t;
			t.name = m_pending_record;
			t.map = m_current_map;
			t.build = COxWare::the().get_build_number();
			t.weapon_id = weapon->m_weapon_id;
			t.origin = CLocalState::the().get_origin();
			t.eye = local->get_eye_pos();
			t.velocity = CLocalState::the().get_local_velocity_vec();
			t.angles = cl->viewangles;
			t.buttons = cmd->buttons;
			t.msec = cmd->msec;

			// remember where this throw actually lands, so a later replay can
			// aim for the same spot instead of blindly reusing the old angles.
			auto sim = simulate(t.eye, t.angles, t.velocity, t.weapon_id);
			t.landing = sim.landing;

			m_throws[t.name] = t;
			save();
			CConsole::the().info("GrenadeHelper: saved throw '{}' (weapon {}, build {}).",
								t.name, t.weapon_id, t.build);
		}
		m_pending_record.clear();
	}
	m_prev_attack = attack;

	if (m_replay_frames > 0)
	{
		auto it = m_throws.find(m_active_throw);
		if (it != m_throws.end())
		{
			const auto& t = it->second;
			cmd->viewangles = m_replay_angles;
			cmd->weaponselect = static_cast<byte>(t.weapon_id);
			if (t.msec != 0)
			{
				cmd->msec = t.msec;
			}

			if (m_replay_frames == 2)
			{
				cmd->buttons &= ~IN_ATTACK;
			}
			else
			{
				cmd->buttons |= IN_ATTACK;
			}
		}

		if (--m_replay_frames <= 0)
		{
			m_restore_weapon_frames = 2;
			m_active_throw.clear();
		}
		return;
	}

	if (m_restore_weapon_frames > 0)
	{
		cmd->weaponselect = static_cast<byte>(m_original_weapon_id);
		--m_restore_weapon_frames;
	}
}

void CGrenadeHelper::on_calc_refdef(hl::ref_params_t* params)
{
	if (!params || m_silent_frames <= 0)
	{
		return;
	}

	if (nade_silent.get_value())
	{
		// The engine has already copied the replay angles into cl->viewangles.
		// Put the player's real view back for rendering only, so the screen does
		// not turn while the server still receives the recorded angles.
		params->viewangles = m_saved_camera_angles;
		CMath::the().angle_vectors(params->viewangles, &params->forward, &params->right, &params->up);
	}

	--m_silent_frames;
}

//-----------------------------------------------------------------------------

// Engine-accurate CS 1.6 grenade prediction. Constants and the step order are
// taken from the well-known HL1 grenade predictor (per-weapon gravity/friction,
// pitch bias, 16u spawn offset, (90 - pitch) * 6 speed, PM_PlayerTrace hull 2).
static void grenade_clip_velocity(const Vector& in, const Vector& normal, Vector& out, float overbounce)
{
	const float backoff = in.Dot(normal) * overbounce;
	out.x = in.x - normal.x * backoff;
	out.y = in.y - normal.y * backoff;
	out.z = in.z - normal.z * backoff;

	constexpr float stop_epsilon = 0.1f;
	if (out.x > -stop_epsilon && out.x < stop_epsilon) out.x = 0.0f;
	if (out.y > -stop_epsilon && out.y < stop_epsilon) out.y = 0.0f;
	if (out.z > -stop_epsilon && out.z < stop_epsilon) out.z = 0.0f;
}

grenade_simulation_t CGrenadeHelper::simulate(const Vector& eye, const Vector& viewangles,
											  const Vector& player_velocity, int weapon_id) const
{
	grenade_simulation_t result;

	auto pmove_ptr = CMemoryHookMgr::the().pmove().get();
	hl::playermove_t* pm = pmove_ptr ? *pmove_ptr : nullptr;
	auto cl = CMemoryHookMgr::the().cl().get();
	if (!pm || !pm->PM_PlayerTrace || !cl)
	{
		result.landing = eye;
		return result;
	}

	float ent_gravity = 0.55f, ent_friction = 0.7f;
	switch (weapon_id)
	{
		case WEAPON_FLASHBANG:
		case WEAPON_SMOKEGRENADE:
			ent_gravity = 0.5f;
			ent_friction = 0.8f;
			break;
		case WEAPON_HEGRENADE:
		default:
			break;
	}
	ent_gravity *= nade_gravity_scale.get_value();
	const float overbounce = (2.0f - ent_friction) * nade_friction.get_value();

	Vector throw_angles = viewangles + cl->punchangle;
	if (throw_angles.x < 0.0f)
	{
		throw_angles.x = -10.0f + throw_angles.x * ((90.0f - 10.0f) / 90.0f);
	}
	else
	{
		throw_angles.x = -10.0f + throw_angles.x * ((90.0f + 10.0f) / 90.0f);
	}

	Vector forward;
	CMath::the().angle_vectors(throw_angles, &forward, nullptr, nullptr);

	Vector origin = eye + forward * 16.0f;

	float velocity_modifier = (90.0f - throw_angles.x) * 6.0f;
	velocity_modifier *= nade_throw_speed.get_value() / 675.0f;
	if (velocity_modifier > 750.0f) velocity_modifier = 750.0f;
	if (velocity_modifier < 0.0f) velocity_modifier = 0.0f;

	Vector velocity = player_velocity + forward * velocity_modifier;

	constexpr float dt = 0.01f;
	const int max_steps = static_cast<int>(nade_preview_time.get_value() / dt);
	const float world_gravity = (pm->movevars && pm->movevars->gravity > 0.0f) ? pm->movevars->gravity : 800.0f;
	const float max_velocity = pm->movevars ? pm->movevars->maxvelocity : 2000.0f;

	result.points.reserve(max_steps);
	result.landing = origin;

	const int old_hull = pm->usehull;
	int bounce_count = 0;

	for (int step = 0; step < max_steps; ++step)
	{
		velocity.z -= ent_gravity * world_gravity * dt;

		for (int i = 0; i < 3; ++i)
		{
			float v = velocity[i];
			if (v > max_velocity) v = max_velocity;
			else if (v < -max_velocity) v = -max_velocity;
			velocity[i] = v;
		}

		Vector move = velocity * dt;
		Vector end = origin + move;

		pm->usehull = 2;
		hl::pmtrace_t trace = pm->PM_PlayerTrace((float*)&origin, (float*)&end, PM_NORMAL, -1);
		pm->usehull = old_hull;

		if (trace.allsolid)
		{
			velocity = Vector(0.0f, 0.0f, 0.0f);
			break;
		}

		origin = trace.endpos;
		result.points.push_back(origin);

		if (trace.fraction >= 1.0f)
		{
			continue;
		}

		Vector clipped;
		grenade_clip_velocity(velocity, trace.plane.normal, clipped, overbounce);
		velocity = clipped * nade_bounce.get_value();

		if (++bounce_count >= 10)
		{
			velocity = Vector(0.0f, 0.0f, 0.0f);
		}

		if (trace.plane.normal.z > 0.7f)
		{
			if (velocity.z < world_gravity * dt)
			{
				velocity.z = 0.0f;
			}
			if (velocity.Length() < 30.0f)
			{
				velocity = Vector(0.0f, 0.0f, 0.0f);
			}
			else
			{
				const float scale = (1.0f - trace.fraction) * dt * 0.9f;
				Vector push_end = origin + velocity * scale;
				pm->usehull = 2;
				hl::pmtrace_t push_trace = pm->PM_PlayerTrace((float*)&origin, (float*)&push_end, PM_NORMAL, -1);
				pm->usehull = old_hull;
				origin = push_trace.endpos;
				result.points.push_back(origin);
			}
		}

		if (velocity.IsZero())
		{
			break;
		}
	}

	pm->usehull = old_hull;
	result.landing = origin;
	return result;
}

Vector CGrenadeHelper::solve_angles_for_landing(const Vector& target, int weapon_id) const
{
	auto local = CLocalState::the().local_player();
	if (!local)
	{
		return Vector(0.0f, 0.0f, 0.0f);
	}

	const Vector eye = local->get_eye_pos();
	const Vector velocity = CLocalState::the().get_local_velocity_vec();
	const Vector delta = target - eye;
	const float base_yaw = std::atan2(delta.y, delta.x) * (180.0f / std::numbers::pi_v<float>);

	float best_pitch = 0.0f, best_yaw = base_yaw, best_error = FLT_MAX;

	auto evaluate = [&](float pitch, float yaw)
	{
		auto sim = simulate(eye, Vector(pitch, yaw, 0.0f), velocity, weapon_id);
		float error = (sim.landing - target).Length();
		if (error < best_error)
		{
			best_error = error;
			best_pitch = pitch;
			best_yaw = yaw;
		}
	};

	for (float pitch = -80.0f; pitch <= 80.0f; pitch += 8.0f)
	{
		for (float dyaw = -8.0f; dyaw <= 8.0f; dyaw += 4.0f)
		{
			evaluate(pitch, base_yaw + dyaw);
		}
	}

	for (float pitch = best_pitch - 4.0f; pitch <= best_pitch + 4.0f; pitch += 1.0f)
	{
		for (float dyaw = -4.0f; dyaw <= 4.0f; dyaw += 1.0f)
		{
			evaluate(pitch, best_yaw + dyaw);
		}
	}

	if (best_error > 512.0f)
	{
		return Vector(0.0f, 0.0f, 0.0f);
	}
	return Vector(best_pitch, best_yaw, 0.0f);
}

void CGrenadeHelper::on_render()
{
	if (!nade_preview.get_value() || CPanic::the().panicking())
	{
		return;
	}

	auto drawlist = g_gui_window_rendering_i->get_current_drawlist();
	if (!drawlist)
	{
		return;
	}

	const CColor trajectory_color(255, 170, 40, 210);
	const CColor landing_color(60, 220, 120, 230);
	const CColor saved_color(120, 170, 255, 200);

	auto draw_arc = [&](const std::vector<Vector>& points, const CColor& color, bool draw_landing)
	{
		Vector2D prev(0.0f, 0.0f);
		bool has_prev = false;

		for (const auto& point : points)
		{
			Vector2D screen;
			if (!CGameUtil::the().world_to_screen(point, screen) || screen.x < -4096.0f || screen.y < -4096.0f)
			{
				has_prev = false;
				continue;
			}

			if (has_prev)
			{
				g_gui_window_rendering_i->render_line(drawlist, prev, screen, color, 1.5f);
			}
			prev = screen;
			has_prev = true;
		}

		if (draw_landing && has_prev)
		{
			g_gui_window_rendering_i->render_circle(drawlist, prev, 5.0f, 16, color, 2.0f);
		}
	};

	auto weapon = CWeapons::the().get_current_weapon();
	auto local = CLocalState::the().local_player();
	if (local && weapon && weapon->is_nade())
	{
		auto cl = CMemoryHookMgr::the().cl().get();
		auto sim = simulate(local->get_eye_pos(), cl->viewangles,
						   CLocalState::the().get_local_velocity_vec(), weapon->m_weapon_id);
		draw_arc(sim.points, trajectory_color, true);
	}

	// landing markers of all saved throws on this map
	for (const auto& [name, t] : m_throws)
	{
		auto sim = simulate(t.eye, t.angles, t.velocity, t.weapon_id);
		if (sim.points.empty())
		{
			continue;
		}

		Vector2D screen;
		if (CGameUtil::the().world_to_screen(t.landing.IsZero() ? sim.landing : t.landing, screen))
		{
			g_gui_window_rendering_i->render_circle(drawlist, screen, 4.0f, 12, saved_color, 1.5f);
			auto font = g_gui_fontmgr_i->get_font(FID_SegoeUI, FSZ_13px);
			if (font)
			{
				g_gui_window_rendering_i->render_text(drawlist, font, screen + Vector2D(6.0f, -6.0f), saved_color, name);
			}
		}
	}
}
