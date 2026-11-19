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
*	The above copyright notice and this permission notice shall be included in
*	all copies or substantial portions of the Software.
*
*	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
*	OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
*	IN THE SOFTWARE.
*/

#ifndef GRENADEHELPER_H
#define GRENADEHELPER_H
#pragma once

#include <filesystem>
#include <map>
#include <vector>

//
// GrenadeHelper -- trajectory preview + record/replay of grenade throws.
//
// The replay is built entirely out of networked usercmd fields: viewangles,
// weaponselect, buttons and msec. Nothing is written into pmove/entity memory,
// so the feature is safe to use on a server. The camera is restored in
// V_CalcRefDef, which makes the replay "silent" (no visible screen turning).
//

extern VarBoolean nade_preview;
extern VarBoolean nade_silent;
extern VarFloat nade_throw_speed;
extern VarFloat nade_gravity_scale;
extern VarFloat nade_bounce;
extern VarFloat nade_friction;
extern VarFloat nade_preview_time;
extern VarFloat nade_throw_tolerance;

struct grenade_throw_t
{
	std::string name;
	std::string map;
	int build = 0;
	int weapon_id = 0;

	Vector origin;
	Vector eye;
	Vector velocity;
	Vector angles;

	unsigned short buttons = 0;
	unsigned char msec = 0;
};

class CGrenadeHelper
{
public:
	DECL_BASIC_CLASS(CGrenadeHelper);

public:
	void initialize_gui();

	// called from CL_CreateMove, after the engine and all other features.
	void update_clientmove(float frametime, hl::usercmd_t* cmd);

	// called from V_CalcRefDef, after the original handler.
	void on_calc_refdef(hl::ref_params_t* params);

	// console commands
	bool arm_record(const std::string& name);
	bool throw_nade(const std::string& name);
	void list_nades() const;
	bool delete_nade(const std::string& name);
	std::vector<std::string> throw_names();

private:
	void on_render();

	void reload_if_needed();
	std::filesystem::path storage_path() const;
	void save();
	void load();

	std::vector<Vector> simulate(const Vector& start, const Vector& initial_velocity) const;
	float engine_gravity() const;

private:
	std::string m_current_map;

	std::map<std::string, grenade_throw_t> m_throws;
	std::string m_pending_record;
	std::string m_active_throw;
	bool m_prev_attack = false;

	int m_replay_frames = 0;
	int m_restore_weapon_frames = 0;
	int m_original_weapon_id = 0;

	Vector m_saved_camera_angles;
	int m_silent_frames = 0;
};

#endif // GRENADEHELPER_H
