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

#ifndef MOVEMENTBOT_H
#define MOVEMENTBOT_H
#pragma once

//
// Autonomous movement bot.
//
// When enabled it fully owns the usercmd for the current frame: it picks a
// destination point, steers the view yaw at it, walks/bunnyhops toward it and
// runs an air-strafe controller that keeps accelerating while turning.
//
// Two usage modes:
//   - no waypoints recorded: free roam, the bot generates a point in front of
//     itself and re-picks it when reached or when it gets stuck;
//   - waypoints recorded with the 'movement_bot_waypoint' command: the bot
//     loops through them in order.
//

extern VarFloat movement_bot_reach_dist;
extern VarFloat movement_bot_roam_distance;
extern VarBoolean movement_bot_use_bhop;
extern VarBoolean movement_bot_use_strafe;
extern VarBoolean movement_bot_tricks;
extern VarInteger movement_bot_max_speed;
extern VarBoolean movement_bot_debug;

class CMovementBot
{
public:
	DECL_BASIC_CLASS(CMovementBot);

	// Returns true when the bot took over the command. CMovement will skip
	// all other movement features in that case.
	bool update(float frametime, hl::usercmd_t *cmd);

	static InCommand toggle;        // movement_bot
	static InCommand add_waypoint;  // movement_bot_waypoint
	static InCommand clear_route;   // movement_bot_route_clear

private:
	void handle_route_input();
	void pick_roam_target(const Vector &origin);
	Vector current_target(const Vector &origin) const;
	void drive_to(const Vector &origin, const Vector &target, hl::usercmd_t *cmd);

private:
	std::vector<Vector> m_route;
	size_t m_route_index = 0;

	Vector m_roam_target;
	float m_roam_timer = 0.0f;
	float m_stuck_timer = 0.0f;

	bool m_was_add_active = false;
	bool m_was_clear_active = false;
	bool m_was_enabled = false;
};

#endif // MOVEMENTBOT_H
