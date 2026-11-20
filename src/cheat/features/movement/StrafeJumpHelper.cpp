/*
*	OXWARE developed by oxiKKK
*	Copyright (c) 2023
*
*	This program is licensed under the MIT license. By downloading, copying,
*	installing or using this software you agree to this license.
*
*	License Agreement
*
*	Permission is hereby granted, free of charge, to any person obtaining a
*	copy of this software and associated documentation files (the "Software"),
*	to deal in the Software without restriction, including without limitation
*	the rights to use, copy, modify, merge, publish, distribute, sublicense,
*	and/or sell copies of the Software, and to permit persons to whom the
*	Software is furnished to do so, subject to the following conditions:
*
*	The above copyright notice and this permission notice shall be included
*	in all copies or substantial portions of the Software.
*
*	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
*	OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
*	THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
*	IN THE SOFTWARE.
*/

#include "precompiled.h"

#include <algorithm>
#include <cmath>
#include <iterator>

VarFloat strafe_jump_tolerance("strafe_jump_tolerance", "Landing spot tolerance, units", 72.0f, 16.0f, 256.0f);
VarFloat strafe_jump_sim_time("strafe_jump_sim_time", "How long to simulate the jump, seconds", 2.0f, 0.5f, 5.0f);
VarFloat strafe_jump_preview_time("strafe_jump_preview_time", "How much of the band to draw, seconds", 1.5f, 0.2f, 5.0f);
VarFloat strafe_jump_jump_velocity("strafe_jump_jump_velocity", "Jump impulse (CS 1.6 default is about 270)", 270.0f, 100.0f, 500.0f);
VarFloat strafe_jump_recalc_interval("strafe_jump_recalc_interval", "How often to re-solve, seconds", 0.5f, 0.1f, 2.0f);
VarBoolean strafe_jump_auto_execute("strafe_jump_auto_execute", "Replay the found input while the bind is held", false);
VarBoolean strafe_jump_show_path("strafe_jump_show_path", "Draw the predicted strafe band", true);
VarBoolean strafe_jump_show_hint("strafe_jump_show_hint", "Draw keys / yaw / reachability hint", true);

InCommand CStrafeJumpHelper::toggle = InCommand(
	"strafe_jump_target", NULL, false,
	IN_ACTCOND_Alive | IN_ACTCOND_NotInMessageMode);

namespace
{
	constexpr float SIM_DT = 0.01f;
	constexpr float HINT_WIDTH = 1.6f;

	float wrap_yaw_deg(float yaw)
	{
		while (yaw > 180.0f) yaw -= 360.0f;
		while (yaw < -180.0f) yaw += 360.0f;
		return yaw;
	}

	float yaw_to_degrees(const Vector& from, const Vector& to)
	{
		return CMath::the().rad2deg(atan2f(to.y - from.y, to.x - from.x));
	}

	void clip_velocity(const Vector& in, const Vector& normal, Vector& out, float overbounce)
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

	float ground_probe(const Vector& origin, int hull)
	{
		auto enginefuncs = CMemoryHookMgr::the().cl_enginefuncs();
		if (!enginefuncs || !enginefuncs->pfnPM_TraceLine)
		{
			return -1.0f;
		}

		Vector start = origin + Vector(0.0f, 0.0f, 4.0f);
		Vector end = origin - Vector(0.0f, 0.0f, 16.0f);
		hl::pmtrace_t* tr = enginefuncs->pfnPM_TraceLine((Vector)start, (Vector)end,
													   PM_TRACELINE_ANYVISIBLE, hull, -1);
		if (!tr || tr->fraction >= 1.0f || tr->plane.normal.z < 0.7f)
		{
			return -1.0f;
		}
		return tr->endpos.z;
	}
}

void CStrafeJumpHelper::initialize_gui()
{
	COxWareUI::the().add_background_rendering_code([this]() { on_render(); });
}

bool CStrafeJumpHelper::capture_from_crosshair(Vector& out) const
{
	auto cl = CMemoryHookMgr::the().cl().get();
	auto pm = CMemoryHookMgr::the().pmove().get();
	auto enginefuncs = CMemoryHookMgr::the().cl_enginefuncs();
	if (!cl || !pm || *pm == nullptr || !enginefuncs || !enginefuncs->pfnPM_TraceLine)
	{
		return false;
	}

	Vector eye = CLocalState::the().local_player()
		? CLocalState::the().local_player()->get_eye_pos()
		: Vector((*pm)->origin) + Vector((*pm)->view_ofs);

	Vector forward;
	CMath::the().angle_vectors(cl->viewangles, &forward, nullptr, nullptr);

	Vector far_point = eye + forward * 4096.0f;
	hl::pmtrace_t* tr = enginefuncs->pfnPM_TraceLine((Vector)eye, (Vector)far_point,
												   PM_TRACELINE_ANYVISIBLE, 2, -1);
	if (!tr || tr->fraction >= 1.0f)
	{
		return false;
	}

	Vector hit = tr->endpos;
	// A strafe lands with the feet, so project the surface hit onto the ground.
	int hull = CLocalState::the().get_current_hull_tracing();
	float ground_z = ground_probe(hit + Vector(0.0f, 0.0f, 8.0f), hull);
	if (ground_z >= 0.0f)
	{
		hit.z = ground_z;
	}
	out = hit;
	return true;
}

void CStrafeJumpHelper::capture_target()
{
	Vector point;
	if (!capture_from_crosshair(point))
	{
		CConsole::the().warning("Jump strafe helper: nothing under the crosshair.");
		return;
	}

	m_target = point;
	m_has_target = true;
	m_executing = false;
	recompute_solution();

	CConsole::the().info("Jump strafe helper: target set to ({:.1f}, {:.1f}, {:.1f}) - {}.",
						 point.x, point.y, point.z,
						 m_solution.reachable ? "reachable" : "not reachable");
}

void CStrafeJumpHelper::clear_target()
{
	m_has_target = false;
	m_executing = false;
	m_solution = strafe_jump_solution_t();
}

strafe_jump_solution_t CStrafeJumpHelper::simulate_candidate(const Vector& start, const Vector& velocity,
															  const Vector& target, float launch_yaw,
															  int strafe_dir, float turn_per_step) const
{
	strafe_jump_solution_t result;
	result.valid = true;
	result.launch_yaw = launch_yaw;
	result.strafe_dir = strafe_dir;
	result.start = start;
	result.target = target;

	auto pmove_ptr = CMemoryHookMgr::the().pmove().get();
	hl::playermove_t* pm = pmove_ptr ? *pmove_ptr : nullptr;
	if (!pm || !pm->PM_PlayerTrace)
	{
		result.valid = false;
		return result;
	}

	const int max_steps = std::clamp(static_cast<int>(strafe_jump_sim_time.get_value() / SIM_DT), 20, 500);
	const float tolerance = strafe_jump_tolerance.get_value();
	const float jump_velocity = strafe_jump_jump_velocity.get_value();

	const float world_gravity = (pm->movevars && pm->movevars->gravity > 0.0f) ? pm->movevars->gravity : 800.0f;
	const float player_gravity = pm->gravity > 0.0f ? pm->gravity : 1.0f;
	const float friction = (pm->movevars && pm->movevars->friction > 0.0f) ? pm->movevars->friction : 4.0f;
	const float ground_accel = pm->movevars ? pm->movevars->accelerate : 10.0f;
	const float air_accel = pm->movevars ? pm->movevars->airaccelerate : 10.0f;
	const float stepsize = (pm->movevars && pm->movevars->stepsize > 0.0f) ? pm->movevars->stepsize : 18.0f;
	float maxspeed = pm->clientmaxspeed > 0.0f ? pm->clientmaxspeed : pm->maxspeed;
	if (maxspeed <= 0.0f) maxspeed = 250.0f;

	const int old_hull = pm->usehull;
	const int hull = CLocalState::the().is_ducking() ? 1 : 0;

	Vector origin = start;
	Vector vel = velocity;
	bool onground = (pm->onground != 0) || (pm->flags & FL_ONGROUND) != 0;
	float yaw = launch_yaw;
	float best_error = 1e30f;
	int frames = max_steps;

	result.steps.reserve(max_steps);
	result.path.reserve(max_steps / 3 + 1);
	result.path.push_back(origin);

	for (int step = 0; step < max_steps; ++step)
	{
		// Ground friction, as in PM_Friction.
		if (onground)
		{
			float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
			if (speed > 0.0f)
			{
				float drop = speed * friction * SIM_DT;
				float scale = (drop >= speed) ? 0.0f : (speed - drop) / speed;
				vel.x *= scale;
				vel.y *= scale;
			}
		}

		Vector forward, right;
		CMath::the().angle_vectors(Vector(0.0f, yaw, 0.0f), &forward, &right, nullptr);

		const float forwardmove = maxspeed;
		const float sidemove = maxspeed * static_cast<float>(strafe_dir);
		Vector wish = forward * forwardmove + right * sidemove;
		wish.z = 0.0f;

		float wishspeed = wish.Length2D();
		if (wishspeed > 0.0f)
		{
			wish = wish * (1.0f / wishspeed);
			if (wishspeed > maxspeed) wishspeed = maxspeed;

			// CS caps the wishspeed used by air acceleration at 30 units.
			float effective_wishspeed = onground ? wishspeed : std::min(wishspeed, 30.0f);
			float currentspeed = vel.x * wish.x + vel.y * wish.y;
			float addspeed = effective_wishspeed - currentspeed;
			if (addspeed > 0.0f)
			{
				float accelspeed = (onground ? ground_accel : air_accel) * SIM_DT * effective_wishspeed;
				if (accelspeed > addspeed) accelspeed = addspeed;
				vel.x += wish.x * accelspeed;
				vel.y += wish.y * accelspeed;
			}
		}

		const bool jump_this_step = (step == 0);
		if (jump_this_step)
		{
			vel.z = jump_velocity;
			onground = false;
		}

		vel.z -= world_gravity * player_gravity * SIM_DT;

		Vector move = vel * SIM_DT;
		Vector end = origin + move;

		pm->usehull = hull;
		hl::pmtrace_t trace = pm->PM_PlayerTrace((float*)&origin, (float*)&end, PM_NORMAL, -1);
		pm->usehull = old_hull;

		if (trace.allsolid)
		{
			break;
		}

		if (trace.fraction < 1.0f && trace.plane.normal.z < 0.7f)
		{
			// Try to step over a low obstacle before treating it as a wall.
			Vector raised_start = origin + Vector(0.0f, 0.0f, stepsize);
			Vector raised_end = end + Vector(0.0f, 0.0f, stepsize);
			pm->usehull = hull;
			hl::pmtrace_t step_trace = pm->PM_PlayerTrace((float*)&raised_start, (float*)&raised_end, PM_NORMAL, -1);
			pm->usehull = old_hull;

			if (step_trace.fraction >= 1.0f)
			{
				origin = raised_end;
			}
			else
			{
				Vector clipped;
				clip_velocity(vel, Vector(trace.plane.normal), clipped, 1.0f);
				vel = clipped;
				origin = origin + move * trace.fraction;
			}
		}
		else
		{
			origin = trace.fraction < 1.0f ? Vector(trace.endpos) : end;

			if (trace.fraction < 1.0f && trace.plane.normal.z >= 0.7f)
			{
				vel.z = 0.0f;
				onground = true;
			}
			else
			{
				onground = false;
			}
		}

		// Snap onto shallow ground: PM_PlayerTrace can miss thin surfaces when
		// the player is descending slowly.
		if (!onground && vel.z <= 0.0f)
		{
			float z = ground_probe(origin, hull);
			if (z >= 0.0f && origin.z - z <= 12.0f)
			{
				origin.z = z;
				vel.z = 0.0f;
				onground = true;
			}
		}

		strafe_jump_step_t recorded;
		recorded.yaw = yaw;
		recorded.forwardmove = forwardmove;
		recorded.sidemove = sidemove;
		recorded.jump = jump_this_step;
		recorded.origin = origin;
		result.steps.push_back(recorded);

		if ((step % 3) == 0 || step == max_steps - 1)
		{
			result.path.push_back(origin);
		}

		float dx = origin.x - target.x;
		float dy = origin.y - target.y;
		float planar = sqrtf(dx * dx + dy * dy);
		float height = fabsf(origin.z - target.z);
		float error = planar + height * 0.25f;
		if (error < best_error)
		{
			best_error = error;
			result.landing = origin;
			result.time = (step + 1) * SIM_DT;
		}

		if (planar <= tolerance && height <= 96.0f)
		{
			result.reachable = true;
			result.error = error;
			frames = step + 1;
			break;
		}

		// Fell far below the target - no point in simulating further.
		if (origin.z < target.z - 512.0f)
		{
			frames = step + 1;
			break;
		}

		yaw = wrap_yaw_deg(yaw + turn_per_step);
	}

	if (result.error > best_error)
	{
		result.error = best_error;
	}
	if (result.time <= 0.0f)
	{
		result.time = frames * SIM_DT;
	}

	// Keep only the frames that were actually simulated.
	result.steps.resize(std::min(static_cast<size_t>(frames), result.steps.size()));
	const size_t preview_steps = std::min(result.steps.size(),
		static_cast<size_t>(std::clamp(static_cast<int>(strafe_jump_preview_time.get_value() / SIM_DT), 1, max_steps)));
	if (!result.steps.empty())
	{
		result.path.resize(std::max<size_t>(1, preview_steps / 3 + 1));
	}
	return result;
}

void CStrafeJumpHelper::recompute_solution()
{
	if (!m_has_target)
	{
		return;
	}

	auto pmove_ptr = CMemoryHookMgr::the().pmove().get();
	hl::playermove_t* pm = pmove_ptr ? *pmove_ptr : nullptr;
	auto cl = CMemoryHookMgr::the().cl().get();
	if (!pm || !cl)
	{
		return;
	}

	const Vector start = Vector(pm->origin);
	const Vector velocity = Vector(pm->velocity);
	const float target_yaw = yaw_to_degrees(start, m_target);

	strafe_jump_solution_t best;
	const float yaw_offsets[] = { -45.0f, -28.0f, -14.0f, 0.0f, 14.0f, 28.0f, 45.0f };
	const float turn_rates[] = { -1.2f, 1.2f };

	for (float offset : yaw_offsets)
	{
		for (int dir : { -1, 1 })
		{
			for (float turn : turn_rates)
			{
				auto test = simulate_candidate(start, velocity, m_target,
											  wrap_yaw_deg(target_yaw + offset), dir, turn * static_cast<float>(dir));
				if (!test.valid)
				{
					continue;
				}

				if (!best.valid || test.error < best.error ||
					(fabsf(test.error - best.error) < 0.5f && test.reachable && !best.reachable))
				{
					best = std::move(test);
				}
			}
		}
	}

	// A plain forward jump is useful for close, straight-line targets.
	auto straight = simulate_candidate(start, velocity, m_target, target_yaw, 0, 0.0f);
	if (straight.valid && (!best.valid || straight.error < best.error))
	{
		best = std::move(straight);
	}

	best.yaw_delta = wrap_yaw_deg(best.launch_yaw - cl->viewangles[YAW]);
	m_solution = std::move(best);
	m_last_solve = std::chrono::steady_clock::now();
}

void CStrafeJumpHelper::update_clientmove(float frametime, hl::usercmd_t* cmd)
{
	bool active = toggle.is_active();
	if (active && !m_was_toggle_active)
	{
		capture_target();
	}
	m_was_toggle_active = active;

	if (m_has_target && !m_executing)
	{
		auto now = std::chrono::steady_clock::now();
		float since = std::chrono::duration<float>(now - m_last_solve).count();
		if (since >= strafe_jump_recalc_interval.get_value())
		{
			recompute_solution();
		}
	}

	if (active && strafe_jump_auto_execute.get_value())
	{
		apply_auto_execution(cmd);
	}
	else if (m_executing)
	{
		m_executing = false;
	}
}

void CStrafeJumpHelper::apply_auto_execution(hl::usercmd_t* cmd)
{
	if (!m_executing)
	{
		m_executing = true;
		m_exec_start = std::chrono::steady_clock::now();
	}

	if (!m_solution.valid || m_solution.steps.empty())
	{
		return;
	}

	float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_exec_start).count();
	size_t index = std::min(static_cast<size_t>(std::max(0.0f, elapsed) / SIM_DT),
							m_solution.steps.size() - 1);
	const auto& step = m_solution.steps[index];

	cmd->viewangles[YAW] = step.yaw;
	cmd->viewangles[PITCH] = 0.0f;
	cmd->forwardmove = step.forwardmove;
	cmd->sidemove = step.sidemove;
	cmd->upmove = 0.0f;

	if (step.jump)
	{
		cmd->buttons |= IN_JUMP;
	}
	else
	{
		cmd->buttons &= ~IN_JUMP;
	}

	if (index >= m_solution.steps.size() - 1)
	{
		m_executing = false;
	}
}

void CStrafeJumpHelper::on_render()
{
	if (!m_has_target || !m_solution.valid || CPanic::the().panicking())
	{
		return;
	}

	auto drawlist = g_gui_window_rendering_i->get_current_drawlist();
	if (!drawlist)
	{
		return;
	}

	const CColor band_color = m_solution.reachable
		? CColor(80, 230, 180, 230)
		: CColor(255, 110, 90, 220);
	const CColor target_color(120, 180, 255, 240);

	if (strafe_jump_show_path.get_value())
	{
		Vector2D prev(0.0f, 0.0f);
		bool has_prev = false;
		for (const auto& point : m_solution.path)
		{
			Vector2D screen;
			if (!CGameUtil::the().world_to_screen(point, screen) ||
				screen.x < -4096.0f || screen.y < -4096.0f)
			{
				has_prev = false;
				continue;
			}
			if (has_prev)
			{
				g_gui_window_rendering_i->render_line(drawlist, prev, screen, band_color, HINT_WIDTH);
			}
			prev = screen;
			has_prev = true;
		}
		if (has_prev)
		{
			g_gui_window_rendering_i->render_circle(drawlist, prev, 5.0f, 16, band_color, 1.5f);
		}
	}

	Vector2D target_screen;
	if (CGameUtil::the().world_to_screen(m_target, target_screen))
	{
		g_gui_window_rendering_i->render_circle(drawlist, target_screen, 6.0f, 20, target_color, 1.5f);
		g_gui_window_rendering_i->render_line(drawlist,
			target_screen + Vector2D(-9.0f, 0.0f), target_screen + Vector2D(9.0f, 0.0f), target_color, 1.0f);
		g_gui_window_rendering_i->render_line(drawlist,
			target_screen + Vector2D(0.0f, -9.0f), target_screen + Vector2D(0.0f, 9.0f), target_color, 1.0f);
	}

	if (!strafe_jump_show_hint.get_value())
	{
		return;
	}

	auto font = g_gui_fontmgr_i->get_font(FID_SegoeUI, FSZ_13px);
	if (!font)
	{
		return;
	}

	auto screen_size = CVideoModeUtil::the().get_real_screen_size();
	Vector2D base(screen_size.x * 0.5f, screen_size.y - 110.0f);

	const char* keys = m_solution.strafe_dir > 0 ? "W + D + Jump"
		: m_solution.strafe_dir < 0 ? "W + A + Jump" : "W + Jump";

	std::string lines[] =
	{
		std::format("Jump strafe: {}", m_solution.reachable ? "REACHABLE" : "NO SOLUTION"),
		std::format("Keys: {}    Yaw: {:+.0f} deg    Time: {:.2f} s", keys, m_solution.yaw_delta, m_solution.time),
		std::format("Miss: {:.0f} units (tolerance {:.0f})", m_solution.error, strafe_jump_tolerance.get_value())
	};

	for (size_t i = 0; i < std::size(lines); ++i)
	{
		g_gui_window_rendering_i->render_text(drawlist, font,
			base + Vector2D(-170.0f, static_cast<float>(i) * 17.0f),
			i == 0 ? band_color : CColor(230, 230, 230, 240), lines[i]);
	}
}
