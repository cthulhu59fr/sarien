/*  Sarien - A Sierra AGI resource interpreter engine
 *  Copyright (C) 1999-2001 Stuart George and Claudio Matsuoka
 *  
 *  $Id$
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; see docs/COPYING for further details.
 */

#include "sarien.h"
#include "agi.h"
#include "rand.h"
#include "gfx_agi.h"
#include "gfx_base.h"		/* FIXME: check this */
#include "keyboard.h"
#include "picture.h"
#include "view.h"
#include "logic.h"
#include "sound.h"
#include "opcodes.h"
#include "console.h"
#include "menu.h"

#define TICK_SECONDS 20
 
extern struct agi_logic logics[];
extern struct agi_view views[];
extern struct agi_view_table view_table[];


static void update_objects ()
{
	int i;

	for(i = 0; i < MAX_VIEWTABLE; i++) {
		struct agi_view_table *vt_obj = &view_table[i];

		if (!(vt_obj->flags & UPDATE))
			continue;

		if ((~vt_obj->flags & ANIMATED) || (~vt_obj->flags & DRAWN))
			continue;

		if (vt_obj->flags & CYCLING) {
			int cel, num_cels;

			vt_obj->cycle_time_count++;

			if (vt_obj->cycle_time_count <= vt_obj->cycle_time) {
				/* --draw_obj(i); */
				continue;
			}

			vt_obj->cycle_time_count = 1;
			cel = vt_obj->current_cel;
			num_cels = VT_LOOP(view_table[i]).num_cels;

			switch (vt_obj->cycle_status) {
			case CYCLE_NORMAL:
				if(++cel >= num_cels) 
					cel = 0;
				break;
 			case CYCLE_END_OF_LOOP:		
 				if(++cel >= num_cels) {
					cel--;
					vt_obj->flags &= ~CYCLING;
					setflag (vt_obj->parm1, TRUE);
				}
 				break;
			case CYCLE_REV:			/* reverse cycle */
				if(--cel < 0)
					cel = num_cels - 1;
				break;
 			case CYCLE_REV_LOOP:
 				if (--cel < 0) {
					cel = 0;
					vt_obj->flags &= ~CYCLING;
					setflag (vt_obj->parm1, TRUE);
 				} 
 				break;
			}
			set_cel (i, cel);
		}
	} 
}


void adj_direction (int entry, int h, int w)
{
	struct agi_view_table *vt_obj;

	if (h == 0 && w == 0)
		return;

	vt_obj = &view_table[entry];

	if (abs(w) > abs(h)) {
		if (w > 0)
			vt_obj->direction = h < 0 ? 2 : h > 0 ? 4 : 3;
		else if (w < 0)
			vt_obj->direction = h < 0 ? 8 : h > 0 ? 6 : 7;
		else
			vt_obj->direction = h <= 0 ? 1 : 5;
	} else {
		if (h > 0)
			vt_obj->direction = w < 0 ? 6 : w > 0 ? 4 : 5;
		else if (h < 0)
			vt_obj->direction = w < 0 ? 8 : w > 0 ? 2 : 1;
		else
			vt_obj->direction = w <= 0 ? 7 : 3;
	}

	/* Always call calc_direction to avoid moonwalks */
	calc_direction (entry);
}


static int check_borders (int em, int x, int y)
{
	struct agi_view_table *vt_obj = &view_table[em];
	int dir, v, cel_width = VT_WIDTH(view_table[em]);

	vt_obj = &view_table[em];
	dir = vt_obj->direction;

	v = em == EGO_VIEW_TABLE ? V_border_touch_ego : V_border_touch_obj;

	if (x < 0 && (dir == 8 || dir == 7 || dir == 6)) {
		_D (_D_WARN "left border: vt %d, x %d, dir %d", em, x, dir); 
		if (em != EGO_VIEW_TABLE)
			setvar (V_border_code, em);
		setvar (v, 4);
		return -1;
	}

	if (x > _WIDTH - cel_width && (dir == 2 || dir == 3 || dir == 4)) {
		_D (_D_WARN "right border: vt %d, x %d, dir %d", em, x, dir); 
		if (em != EGO_VIEW_TABLE)
			setvar (V_border_code, em);
		setvar (v, 2);
		return -1;
	}

	if (y > _HEIGHT - 1 && (dir == 4 || dir == 5 || dir == 6)) {
		_D (_D_WARN "bottom border: vt %d, x %d, dir %d", em, x, dir); 
		if (em != EGO_VIEW_TABLE)
			setvar (V_border_code, em);
		setvar (v, 3);
		return -1;
	}

	if (y < game.horizon && (dir == 1 || dir == 2 || dir == 8)) {
		_D (_D_WARN "top border: vt %d, x %d, dir %d", em, x, dir); 
		if (em != EGO_VIEW_TABLE)
			setvar (V_border_code, em);
		setvar (v, 1);
		return -1;
	}

	return 0;
}


static int check_control_lines (int em, int x, int y)
{
	struct agi_view_table *vt_obj = &view_table[em];
	int i, w, cel_width = VT_WIDTH(view_table[em]);

	if (em == EGO_VIEW_TABLE)
		setflag (F_ego_touched_p2, FALSE);

	w = 0;
	for (i = x + cel_width - 1; i >= x; i--) {
		switch (xdata_data[y * _WIDTH + i]) {
		case 0:	/* unconditional black. no go at all! */
			return -1;
		case 1:			/* conditional blue */
			if (~vt_obj->flags & IGNORE_BLOCKS) {
				_D (_D_CRIT "Blocks observed!");
				return -1;
			}
			break;
		case 2:			/* trigger */
			if (em == EGO_VIEW_TABLE) {
				setflag (3, TRUE);
				vt_obj->x_pos = x;
				vt_obj->y_pos = y;
				_D (_D_CRIT "Trigger pressed!");
				return -2;
			}
		}
	}

	return 0;
}


static int check_surface (int em, int x, int y)
{
	struct agi_view_table *vt_obj = &view_table[em];
	int i, w, cel_width = VT_WIDTH(view_table[em]);

	if (em == EGO_VIEW_TABLE)
		setflag (F_ego_water, FALSE);

	w = 0;
	for (i = x + cel_width - 1; i >= x; i--) {
		if (xdata_data[y * _WIDTH + i] == 3 && em == EGO_VIEW_TABLE)
			w++;
	}

	if (em == EGO_VIEW_TABLE) {
		/* Check if ego is completely on water */
		if (w >= cel_width) {
			_D (_D_WARN "Ego is completely on water");
			vt_obj->x_pos = x;
			vt_obj->y_pos = y;
			setflag (F_ego_water, TRUE);
			return -1;
		}
	}

	for (i = x + cel_width - 1; i >= x; i--) {
		int z;

		if (y < game.horizon || y >= _HEIGHT || i < 0 || i >= _WIDTH)
			return -1;

		z = xdata_data[y * _WIDTH + i];

		if ((vt_obj->flags & ON_WATER) && z != 3) {
			_D (_D_CRIT "failed: must stay ON_WATER");
			return -1;
		}

		if ((vt_obj->flags & ON_LAND) && z == 3) {
			_D (_D_CRIT "failed: must stay ON_LAND");
			return -1;
		}
	}

	return 0;
}


static void normal_motion (int em, int x, int y)
{
	struct agi_view_table *vt_obj;

	if (VT_VIEW(view_table[em]).loop == NULL) {
		_D(_D_CRIT "Attempt to access NULL view_table[%d].loop", em);
		return;
	}

	if (view_table[em].current_cel >= VT_LOOP(view_table[em]).num_cels) {
		_D(_D_CRIT "Attempt to access cel(%d) >= num_cels(%d) in vt%d",
			view_table[em].current_cel,
			VT_LOOP(view_table[em]).num_cels, em);
		return;
	}

	vt_obj = &view_table[em];

	/* Positions should be adjusted if the object is stepping
	 * on a control line, as reported by Nat Budin.
	 *
	 * This is not the correct solution: it breaks DDP and KQ1 intros
	 * but fixes hooker and honeymoon suite door in LSL1.
	 *
	 * "I found that the object would usually go further down and to
	 * the left than I told AGI to place it. This wasn't completely
	 * consistent, though.  You might want to email Nick Sonneveld
	 * about this, because his interpreter seems to place the views
	 * correctly."
	 */

	/* Handles the "Budin offset" */
	while (check_control_lines (em, vt_obj->x_pos, vt_obj->y_pos) == -1) {
		if (vt_obj->x_pos > 0)
			vt_obj->x_pos--;
		else
			break;

		if (check_control_lines (em, vt_obj->x_pos, vt_obj->y_pos) == 0)
			break;

		if (vt_obj->y_pos < _HEIGHT)
			vt_obj->y_pos++;
		else
			break;
	}

	x += vt_obj->x_pos;
	y += vt_obj->y_pos;

	if (check_borders (em, x, y) || check_control_lines (em, x, y) ||
		check_surface (em, x, y))
		return;

	/* New object direction */
	adj_direction (em, y - vt_obj->y_pos, x - vt_obj->x_pos);

	vt_obj->x_pos = x;
	vt_obj->y_pos = y;
}


static void set_motion (int em)
{
	int i, mt[9][2] = {
		{  0,  0 }, {  0, -1 }, {  1, -1 }, {  1,  0 },
		{  1,  1 }, {  0,  1 }, { -1,  1 }, { -1,  0 },
		{ -1, -1 }
	};

	i = view_table[em].direction;

	normal_motion (em, mt[i][0], mt[i][1]);
}


static void adj_pos (int em, int x2, int y2)
{
	int x1, y1, z;
	static int frac = 0;
	struct agi_view_table *vt_obj;

	vt_obj = &view_table[em];

	x1 = vt_obj->x_pos;
	y1 = vt_obj->y_pos;

	/* step size is given * 4, must compute fractionary step increments */
	if (vt_obj->motion == MOTION_FOLLOW_EGO) {
		z = vt_obj->step_size >> 2;
		frac += vt_obj->step_size & 0x03;
		if (frac >= 4) {
			frac -= 4;
			z++;
		}
	} else {
		z = vt_obj->step_size;
	}

	/* adjust the direction */
	adj_direction (em, y2 - y1, x2 - x1);

	check_borders (em, x1, y2);
	check_control_lines (em, x1, y2);
	check_surface (em, x1, y2);

#define CLAMP_MAX(a,b,c) { if(((a)+=(c))>(b)) { (a)=(b); } }
#define CLAMP_MIN(a,b,c) { if(((a)-=(c))<(b)) { (a)=(b); } }
#define CLAMP(a,b,c) { if((a)<(b)) CLAMP_MAX(a,b,c) else CLAMP_MIN(a,b,c) }

	CLAMP (x1, x2, z);
	CLAMP (y1, y2, z);

	vt_obj->x_pos = x1;
	vt_obj->y_pos = y1;
}


static void calc_obj_motion ()
{
	int original_direction;
	int em, ox, oy;
	struct agi_view_table *vt_obj, *vt_ego;

	vt_ego = &view_table[EGO_VIEW_TABLE];

	for (em = 0; em < MAX_VIEWTABLE; em++) {
		vt_obj = &view_table[em];

		original_direction = vt_obj->direction;

		if ((~vt_obj->flags & MOTION) || (~vt_obj->flags & UPDATE))
			continue;

		vt_obj->step_time_count += vt_obj->step_size;

		if(vt_obj->step_time_count <= vt_obj->step_time)
			continue;

		vt_obj->step_time_count=1;

		switch(vt_obj->motion) {
		case MOTION_NORMAL:		/* normal */
			set_motion (em);
			break;
		case MOTION_WANDER:		/* wander */
			ox = vt_obj->x_pos;
			oy = vt_obj->y_pos;
			set_motion (em);

			/* CM: FIXME: when object walks offscreen (x < 0)
			 *     it returns x_pos == ox, y_pos == oy and
			 *     the last frame blitted shows object facing
			 *     another direction.
			 */
			if (vt_obj->x_pos == ox && vt_obj->y_pos == oy)
				vt_obj->direction = rnd (9);
			break;
		case MOTION_FOLLOW_EGO:		/* follow ego */
			adj_pos (em, vt_ego->x_pos, vt_ego->y_pos);

			if (vt_obj->x_pos == vt_ego->x_pos &&
				vt_obj->y_pos == vt_ego->y_pos)
			{
				vt_obj->motion = MOTION_NORMAL;
				vt_obj->flags &= ~MOTION;
				setflag (vt_obj->parm2, TRUE);
				vt_obj->parm2 = 1;
				vt_obj->step_size = vt_obj->parm1;
			}
			break;
		case MOTION_MOVE_OBJ:		/* move obj */
			adj_pos (em, vt_obj->parm1, vt_obj->parm2);

			if (vt_obj->x_pos == vt_obj->parm1 &&
				vt_obj->y_pos == vt_obj->parm2)
			{
				_D (_D_WARN "obj %d at (%d, %d), set %d!", em,
					vt_obj->x_pos, vt_obj->y_pos,
					vt_obj->parm4);
				if (em == EGO_VIEW_TABLE)
					setvar (V_ego_dir, 0);

				vt_obj->direction = 0;
				setflag (vt_obj->parm4, TRUE);
				vt_obj->step_size = vt_obj->parm3;
				vt_obj->flags &= ~MOTION;
				vt_obj->motion = MOTION_NORMAL;
			}

			break;
		}

		if (vt_obj->direction != original_direction)
   			calc_direction (em);
	}
}


static void interpret_cycle ()
{
	int line_prompt = FALSE;

	if (game.control_mode == CONTROL_PROGRAM)
		view_table[EGO_VIEW_TABLE].direction = getvar (V_ego_dir);
	else
		setvar (V_ego_dir, view_table[EGO_VIEW_TABLE].direction);

	update_status_line (FALSE);

	release_sprites ();

	do {
		/* set current start IP */
		logics[0].cIP = logics[0].sIP;

		run_logic (0);

		/* make sure logic 0 is not set to 'firsttime' */
		setflag (F_logic_zeron_firsttime, FALSE);

		/* CM: commented out -- setvar (V_key, 0x0); */
		setvar (V_word_not_found, 0);
		setvar (V_border_code, 0);			/* 5 */
		setvar (V_border_touch_obj, 0);			/* 4 */
		setflag (F_entered_cli, FALSE);
		setflag (F_said_accepted_input, FALSE);
		setflag (F_new_room_exec, FALSE);		/* 5 */
		setflag (F_output_mode, FALSE);	/*************************/
		setflag (F_restart_game, FALSE);		/* 6 */
		setflag (F_status_selects_items, FALSE);	/* 12? */

		if (game.control_mode == CONTROL_PROGRAM) {
			view_table[EGO_VIEW_TABLE].direction = getvar (V_ego_dir);
		} else {
			setvar (V_ego_dir, view_table[EGO_VIEW_TABLE].direction);
		}

		clean_input (); 

#ifndef NO_DEBUG
		/* quit built in debugger command */
		if (opt.debug == 3 || opt.debug == 4)
			opt.debug = TRUE;
#endif

		if (game.quit_prog_now)
			break;

		update_status_line (FALSE);

		if (game.ego_in_new_room) {
			_D (_D_WARN "ego_in_new_room");
			new_room (game.new_room_num);
			update_status_line (TRUE);
			game.ego_in_new_room = FALSE;
			line_prompt = TRUE;
		} else {
			if (screen_mode == GFX_MODE) {
				calc_obj_motion ();
				update_objects ();
			}
			break;
		}

		/* CM: hmm... it looks strange :\ */
		game.exit_all_logics = FALSE;
	} while (!game.exit_all_logics);

	redraw_sprites ();

	if (line_prompt)
		print_line_prompt();
}


void update_timer ()
{
	if (!game.clock_enabled)
		return;

	clock_count++;
	if (clock_count <= TICK_SECONDS)
		return;

	clock_count -= TICK_SECONDS;
	setvar (V_seconds, getvar (V_seconds) + 1);
	if (getvar (V_seconds) < 60)
		return;

	setvar (V_seconds, 0);
	setvar (V_minutes, getvar (V_minutes) + 1);
	if (getvar (V_minutes) < 60)
		return;

	setvar (V_minutes, 0);
	setvar (V_hours, getvar (V_hours) + 1);
	if (getvar (V_hours) < 24)
		return;

	setvar (V_hours, 0);
	setvar (V_days, getvar (V_days) + 1);
}


static int old_mode = -1;
void new_input_mode (int i)
{
	old_mode = game.input_mode;
	game.input_mode = i;
}

void old_input_mode ()
{
	game.input_mode = old_mode;
}

/* If main_cycle returns FALSE, don't process more events! */
int main_cycle ()
{
	int key, kascii;

	poll_timer ();		/* msdos driver -> does nothing */
	update_timer ();

	key = poll_keyboard ();
	kascii = KEY_ASCII (key);

	if (!console_keyhandler (key)) {
		if (kascii) setvar (V_key, kascii);
		switch (game.input_mode) {
		case INPUT_NORMAL:
			handle_controller (key);
			handle_keys (key);
			if (key) game.keypress = key;
			break;
		case INPUT_GETSTRING:
			handle_controller (key);
			handle_getstring (key);
			break;
		case INPUT_MENU:
			menu_keyhandler (key);
			console_cycle ();
			return FALSE;
		case INPUT_NONE:
			handle_controller (key);
			if (key) game.keypress = key;
			break;
		}
	} else {
		if (game.input_mode == INPUT_MENU) {
			console_cycle ();
			return FALSE;
		}
	}

	console_cycle ();

	if (getvar (V_window_reset) > 0) {
		game.msg_box_ticks = getvar (V_window_reset) * 10;
		setvar (V_window_reset, 0);
	}

	if (game.msg_box_ticks > 0)
		game.msg_box_ticks--;

	return TRUE;
}


int run_game2 ()
{
	int ec = err_OK;
	UINT32 x, y, z = 12345678;

	_D ("()");

	stop_sound ();
	clear_buffer ();

	/*setflag(F_logic_zeron_firsttime, TRUE);*/
	setflag (F_new_room_exec, TRUE);
	setflag (F_restart_game, FALSE);
	setflag (F_sound_on, TRUE);		/* enable sound */
	setvar (V_time_delay, 2);		/* "normal" speed */

	game.new_room_num = 0;
	game.quit_prog_now = FALSE;
	game.clock_enabled = TRUE;
	game.ego_in_new_room = FALSE;
	game.exit_all_logics = FALSE;
	game.input_mode = INPUT_NONE;

	report (" \nSarien " VERSION " is ready.\n");
	report ("Running AGI script.\n");

#ifdef USE_CONSOLE
	console.count = 20;
	console_prompt ();
#endif

	/* clean_keyboard (); */

	do {
		if (!main_cycle ())
			continue;

		x = 1 + clock_count;		/* x = 1..TICK_SECONDS */
		y = getvar (V_time_delay);	/* 1/20th of second delay */

		if (y == 0 || (x % y == 0 && z != x)) {
			z = x;
			interpret_cycle ();
			/* clean_keyboard (); */
		}

		do_blit ();

		if (game.quit_prog_now == 0xff) {
			ec = err_RestartGame;
			break;
		}
	} while (!game.quit_prog_now);

	stop_sound ();
	clear_buffer ();
	put_screen ();

	return ec;
}

