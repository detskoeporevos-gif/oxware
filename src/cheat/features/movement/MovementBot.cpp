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

//
// MovementBot.cpp -- fully autonomous movement controller.
//
// The bot does not patch pmove fields: it only writes the usercmd, which is
// the only movement state that actually reaches the server. Everything else
// (onground/velocity/waterlevel) is read from CLocalState, i.e. from the
// engine's own prediction.
//

#include "precompiled.h"

VarFloat movement_bot_reach_dist("movement_bot_reach_dist", "Distance at which a waypoint is considered reached", 48.0f, 16.0f, 160.0f);
VarFloat movement_bot_roam_distance("movement_bot_roam_distance", "How far the bot picks its free-roam target", 320.0f, 64.0f, 1024.0f);
VarBoolean movement_bot_use_bhop("movement_bot_use_bhop", "Jump automatically on the first grounded frame", true);
VarBoolean movement_bot_use_strafe("movement_bot_use_strafe", "Air-strafe toward the target instead of just holding forward", true);
VarInteger movement_bot_max_speed("movement_bot_max_speed", "Wish speed used by the bot (0 = use pmove->maxspeed)", 0, 0, 2000);
VarBoolean movement_bot_debug("movement_bot_debug", "Log waypoint recording / route changes", false);

InCommand CMovementBot::toggle = InCommand("movement_bot", NULL, false, IN_ACTCOND_Alive | IN_ACTCOND_NotInMessageMode);
InCommand CMovementBot::add_waypoint = InCommand("movement_bot_waypoint", NULL, false, IN_ACTCOND_Alive | IN_ACTCOND_NotInMessageMode);
InCommand CMovementBot::clear_route = InCommand("movement_bot_route_clear", NULL, false, IN_ACTCOND_None);

//-----------------------------------------------------------------------------

static inline float bot_wrap_pi(float a)
{
	constexpr float two_pi = 2.0f * std::numbers::pi_v<float>;
	while (a > std::numbers::pi_v<float>) a -= two_pi;
	while (a < -std::numbers::pi_v<float>) a += two_pi;
	return a;
}

static inline float bot_yaw(const Vector &v)
{
	return atan2f(v.y, v.x);
}

//-----------------------------------------------------------------------------

void CMovementBot::handle_route_input()
{
	bool add_now = add_waypoint.is_active();
	if (add_now && !m_was_add_active)
	{
		auto origin = CLocalState::the().get_origin();
		if (!origin.IsZero())
		{
			m_route.push_back(origin);
			if (movement_bot_debug.get_value())
			{
				CConsole::the().info("MovementBot: waypoint #{} added ({:.1f}, {:.1f}, {:.1f})",
									m_route.size(), origin.x, origin.y, origin.z);
			}
		}
	}
	m_was_add_active = add_now;

	bool clear_now = clear_route.is_active();
	if (clear_now && !m_was_clear_active)
	{
		m_route.clear();
		m_route_index = 0;
		m_roam_target = vec3_origin;
		m_roam_timer = 0.0f;

		if (movement_bot_debug.get_value())
		{
			CConsole::the().info("MovementBot: route cleared, free-roam mode.");
		}
	}
	m_was_clear_active = clear_now;
}

//-----------------------------------------------------------------------------

void CMovementBot::pick_roam_target(const Vector &origin)
{
	auto enginefuncs = CMemoryHookMgr::the().cl_enginefuncs();
	float view_yaw = CMath::the().deg2rad(CMemoryHookMgr::the().cl().get()->viewangles[YAW]);

	// pick a point within +-90 degrees of the current view
	float offset = CMath::the().deg2rad((float)enginefuncs->pfnRandomLong(-90, 90));
	float yaw = bot_wrap_pi(view_yaw + offset);
	float dist = movement_bot_roam_distance.get_value();

	m_roam_target = origin + Vector(cosf(yaw) * dist, sinf(yaw) * dist, 0.0f);
	m_roam_timer = 0.0f;
}

//-----------------------------------------------------------------------------

Vector CMovementBot::current_target(const Vector &origin) const
{
	if (!m_route.empty())
	{
		return m_route[m_route_index % m_route.size()];
	}

	if (m_roam_target.IsZero() || (m_roam_target - origin).Length2D() < movement_bot_reach_dist.get_value())
	{
		// const_cast is fine here: the roam point is an internal cache, not bot state
		// visible to the rest of the cheat.
		const_cast<CMovementBot*>(this)->pick_roam_target(origin);
	}

	return m_roam_target;
}

//-----------------------------------------------------------------------------

void CMovementBot::drive_to(const Vector &origin, const Vector &target, hl::usercmd_t *cmd)
{
	auto &local = CLocalState::the();

	Vector to = target - origin;
	to.z = 0.0f;

	float target_yaw = bot_yaw(to);
	float target_yaw_deg = CMath::the().rad2deg(bot_wrap_pi(target_yaw));

	// aim the movement basis at the destination. Pitch is flattened on purpose:
	// movement uses only the horizontal part of forward/right.
	cmd->viewangles[YAW] = target_yaw_deg;
	cmd->viewangles[PITCH] = 0.0f;

	float max_speed = (float)movement_bot_max_speed.get_value();
	if (max_speed <= 0.0f)
	{
		max_speed = local.get_maxspeed();
	}

	bool onground = local.is_on_ground_safe();
	bool in_water = local.get_waterlevel() >= 2;
	bool on_ladder = local.get_movetype() == MOVETYPE_FLY;

	cmd->forwardmove = cmd->sidemove = cmd->upmove = 0.0f;

	// jump only while grounded; releasing in air avoids the pogo check
	if (movement_bot_use_bhop.get_value() && onground && !in_water && !on_ladder)
	{
		cmd->buttons |= IN_JUMP;
	}
	else
	{
		cmd->buttons &= ~IN_JUMP;
	}

	if (on_ladder)
	{
		// ladder movement in PM_LadderMove is button-based; IN_JUMP dismounts
		cmd->buttons &= ~IN_FORWARD;
		cmd->buttons &= ~IN_BACK;
		cmd->buttons |= IN_FORWARD;
		return;
	}

	if (in_water)
	{
		cmd->forwardmove = max_speed * 0.8f;
		return;
	}

	if (onground || !movement_bot_use_strafe.get_value())
	{
		// view yaw already points at the target
		cmd->forwardmove = max_speed;
		return;
	}

	//
	// Air control:
	//   - if we already fly at the target, add speed straight ahead;
	//   - otherwise put wishdir to the side of the current velocity, which
	//     gives the maximum PM_AirAccelerate gain while rotating us toward target.
	//
	float vel_yaw = bot_yaw(local.get_local_velocity_vec());
	float delta = bot_wrap_pi(target_yaw - vel_yaw);

	float wish_yaw;
	if (fabsf(delta) < 0.20f)
	{
		wish_yaw = target_yaw;
	}
	else
	{
		float turn = std::min(fabsf(delta), std::numbers::pi_v<float> / 2.0f);
		wish_yaw = vel_yaw + copysignf(turn, delta);
	}

	float rel = bot_wrap_pi(wish_yaw - target_yaw); // view yaw == target_yaw
	cmd->forwardmove = max_speed * cosf(rel);
	cmd->sidemove = max_speed * sinf(rel);
}

//-----------------------------------------------------------------------------

bool CMovementBot::update(float frametime, hl::usercmd_t *cmd)
{
	handle_route_input();

	if (!toggle.is_active())
	{
		m_was_enabled = false;
		return false;
	}

	auto &local = CLocalState::the();
	Vector origin = local.get_origin();
	if (origin.IsZero())
	{
		return false;
	}

	if (!m_was_enabled)
	{
		m_stuck_timer = 0.0f;
		m_roam_timer = 0.0f;
		m_was_enabled = true;
	}

	Vector target = current_target(origin);

	float reach = movement_bot_reach_dist.get_value();
	if ((target - origin).Length2D() < reach)
	{
		if (!m_route.empty())
		{
			m_route_index = (m_route_index + 1) % m_route.size();
		}
		else
		{
			pick_roam_target(origin);
		}

		target = current_target(origin);
	}

	// if we keep asking for movement but do not move, re-pick the goal
	if (local.get_local_velocity_2d() < 30.0f)
	{
		m_stuck_timer += frametime;
	}
	else
	{
		m_stuck_timer = 0.0f;
	}

	m_roam_timer += frametime;
	if (m_stuck_timer > 1.5f || m_roam_timer > 12.0f)
	{
		m_stuck_timer = 0.0f;
		if (m_route.empty())
		{
			pick_roam_target(origin);
		}
		else
		{
			m_route_index = (m_route_index + 1) % m_route.size();
		}

		target = current_target(origin);
	}

	drive_to(origin, target, cmd);
	return true;
}
