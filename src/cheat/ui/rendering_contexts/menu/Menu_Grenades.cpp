/*
*	OXWARE developed by oxiKKK
*	Copyright (c) 2023
*
*	This program is licensed under the MIT license. By downloading, copying,
*	installing or using this software you agree to this license.
*/

#include "precompiled.h"

//---------------------------------------------------------------------------------------------------

void MenuChilden::Grenades::GrenadeHelper::contents()
{
	static char name_buffer[64] = "flash_a";

	g_gui_widgets_i->add_text("Throw name");
	g_gui_widgets_i->add_text_input_ex("##nade_name", name_buffer, sizeof(name_buffer), Vector2D(-1.0f, 0.0f));

	if (g_gui_widgets_i->add_button("Record next throw", { -1.0f, 0.0f }, false, BUTTONFLAG_CenterLabel))
	{
		CGrenadeHelper::the().arm_record(name_buffer);
	}
	if (g_gui_widgets_i->add_button("Throw saved nade", { -1.0f, 0.0f }, false, BUTTONFLAG_CenterLabel))
	{
		CGrenadeHelper::the().throw_nade(name_buffer);
	}

	g_gui_widgets_i->add_spacing();
	g_gui_widgets_i->add_separtor_with_text("Visualization");

	CUIMenuWidgets::the().add_checkbox("Trajectory preview", &nade_preview);
	CUIMenuWidgets::the().add_checkbox("Silent replay", &nade_silent);
	CUIMenuWidgets::the().add_slider("Throw speed", "%0.0f", &nade_throw_speed);
	CUIMenuWidgets::the().add_slider("Gravity scale", "%0.2f", &nade_gravity_scale);
	CUIMenuWidgets::the().add_slider("Bounce", "%0.2f", &nade_bounce);
	CUIMenuWidgets::the().add_slider("Friction", "%0.2f", &nade_friction);
	CUIMenuWidgets::the().add_slider("Preview time", "%0.1f s", &nade_preview_time);
	CUIMenuWidgets::the().add_slider("Spot tolerance", "%0.0f u", &nade_throw_tolerance);

	g_gui_widgets_i->add_spacing();
	g_gui_widgets_i->add_separtor_with_text("Saved throws (current map)");

	auto names = CGrenadeHelper::the().throw_names();
	if (names.empty())
	{
		g_gui_widgets_i->add_text("None. Use 'Record next throw' and then throw a grenade.");
		return;
	}

	for (const auto& name : names)
	{
		g_gui_widgets_i->add_text(name);
		if (g_gui_widgets_i->add_button("Throw##throw_" + name, { -1.0f, 0.0f }, false, BUTTONFLAG_CenterLabel))
		{
			CGrenadeHelper::the().throw_nade(name);
		}
		if (g_gui_widgets_i->add_button("Delete##del_" + name, { -1.0f, 0.0f }, false, BUTTONFLAG_CenterLabel))
		{
			CGrenadeHelper::the().delete_nade(name);
		}
		g_gui_widgets_i->add_spacing();
	}
}

void MenuChilden::Grenades::MovementBot::contents()
{
	auto always = CMovementBot::toggle.get_always_enabled_var();
	CUIMenuWidgets::the().add_checkbox("Always enabled", always);

	if (g_gui_widgets_i->add_button(
		CMovementBot::toggle.is_active() ? "Stop bot" : "Start bot",
		{ -1.0f, 0.0f }, false, BUTTONFLAG_CenterLabel))
	{
		CMovementBot::toggle.set_is_active(!CMovementBot::toggle.is_active());
	}

	g_gui_widgets_i->add_spacing();
	CUIMenuWidgets::the().add_checkbox("Debug", &movement_bot_debug);
	CUIMenuWidgets::the().add_checkbox("Bunnyhop", &movement_bot_use_bhop);
	CUIMenuWidgets::the().add_checkbox("Air strafe", &movement_bot_use_strafe);
	CUIMenuWidgets::the().add_checkbox("Tricks (JOF + EdgeBug)", &movement_bot_tricks);

	g_gui_widgets_i->add_spacing();
	CUIMenuWidgets::the().add_slider("Reach distance", "%0.0f u", &movement_bot_reach_dist);
	CUIMenuWidgets::the().add_slider("Roam distance", "%0.0f u", &movement_bot_roam_distance);
	CUIMenuWidgets::the().add_slider("Speed (0 = auto)", "%0.0f u", &movement_bot_max_speed);

	g_gui_widgets_i->add_spacing();
	g_gui_widgets_i->add_separtor_with_text("Waypoints");

	// one-frame pulses, so the InCommand sees a clean 0 -> 1 -> 0 transition
	static int add_waypoint_pulse = 0, clear_route_pulse = 0;
	if (add_waypoint_pulse > 0)
	{
		CMovementBot::add_waypoint.set_is_active(add_waypoint_pulse == 2);
		--add_waypoint_pulse;
	}
	if (clear_route_pulse > 0)
	{
		CMovementBot::clear_route.set_is_active(clear_route_pulse == 2);
		--clear_route_pulse;
	}

	if (g_gui_widgets_i->add_button("Add waypoint", { -1.0f, 0.0f }, false, BUTTONFLAG_CenterLabel))
	{
		add_waypoint_pulse = 2;
	}
	if (g_gui_widgets_i->add_button("Clear route", { -1.0f, 0.0f }, false, BUTTONFLAG_CenterLabel))
	{
		clear_route_pulse = 2;
	}
}
