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

#ifndef STRAFE_JUMP_HELPER_H
#define STRAFE_JUMP_HELPER_H
#pragma once

#include <chrono>
#include <vector>

//
// Jump/strafe band helper.
//
// The bind (or the "Capture target" button) stores the point under the
// crosshair. The helper then searches over launch yaw / strafe direction /
// air turn combinations, simulates the resulting player movement with the
// engine traces and draws the best predicted band. Optionally it can replay
// the found input through the usercmd while the bind is held.
//

extern VarFloat strafe_jump_tolerance;
extern VarFloat strafe_jump_sim_time;
extern VarFloat strafe_jump_preview_time;
extern VarFloat strafe_jump_jump_velocity;
extern VarFloat strafe_jump_recalc_interval;
extern VarBoolean strafe_jump_auto_execute;
extern VarBoolean strafe_jump_show_path;
extern VarBoolean strafe_jump_show_hint;

struct strafe_jump_step_t
{
	float yaw = 0.0f;
	float forwardmove = 0.0f;
	float sidemove = 0.0f;
	bool jump = false;
	Vector origin;
};

struct strafe_jump_solution_t
{
	bool valid = false;
	bool reachable = false;
	float error = 1e30f;
	float time = 0.0f;
	float launch_yaw = 0.0f;
	float yaw_delta = 0.0f;
	int strafe_dir = 0; // -1 = A, +1 = D, 0 = straight
	Vector start;
	Vector target;
	Vector landing;
	std::vector<strafe_jump_step_t> steps;
	std::vector<Vector> path;
};

class CStrafeJumpHelper
{
public:
	DECL_BASIC_CLASS(CStrafeJumpHelper);

	// Press to capture the crosshair point; hold it to execute the solution
	// when "Auto execute" is enabled.
	static InCommand toggle;

	void initialize_gui();
	void on_render();

	void update_clientmove(float frametime, hl::usercmd_t* cmd);
	void capture_target();
	void clear_target();

	bool has_target() const { return m_has_target; }
	bool is_executing() const { return m_executing; }
	const strafe_jump_solution_t& solution() const { return m_solution; }
	Vector target() const { return m_target; }

private:
	bool capture_from_crosshair(Vector& out) const;
	void recompute_solution();
	strafe_jump_solution_t simulate_candidate(const Vector& start, const Vector& velocity,
											  const Vector& target, float launch_yaw,
											  int strafe_dir, float turn_per_step) const;
	void apply_auto_execution(hl::usercmd_t* cmd);

	bool m_has_target = false;
	bool m_was_toggle_active = false;
	bool m_executing = false;
	Vector m_target;
	strafe_jump_solution_t m_solution;
	std::chrono::steady_clock::time_point m_last_solve;
	std::chrono::steady_clock::time_point m_exec_start;
};

#endif // STRAFE_JUMP_HELPER_H
