/*****************************************************************************
 * Copyright (c) 2014 Ted John
 * OpenRCT2, an open source clone of Roller Coaster Tycoon 2.
 *
 * This file is part of OpenRCT2.
 *
 * OpenRCT2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#include <string.h>
#include "addresses.h"
#include "strings.h"
#include "sprites.h"
#include "viewport.h"
#include "widget.h"
#include "window.h"
#include "window_dropdown.h"

typedef struct {
	uint16 pad_00;
	uint32 image;		// 0x02
	uint32 pad_06;
	uint8 pad_0A;
	uint8 flags;		// 0x0B
} rct_path_type;

static enum WINDOW_FOOTPATH_WIDGET_IDX {
	WIDX_BACKGROUND,
	WIDX_TITLE,
	WIDX_CLOSE,

	WIDX_TYPE_GROUP,
	WIDX_FOOTPATH_TYPE,
	WIDX_QUEUELINE_TYPE,

	WIDX_DIRECTION_GROUP,
	WIDX_DIRECTION_NW,
	WIDX_DIRECTION_NE,
	WIDX_DIRECTION_SW,
	WIDX_DIRECTION_SE,

	WIDX_SLOPE_GROUP,
	WIDX_SLOPEDOWN,
	WIDX_LEVEL,
	WIDX_SLOPEUP,
	WIDX_CONSTRUCT,
	WIDX_REMOVE,

	WIDX_MODE_GROUP,
	WIDX_CONSTRUCT_ON_LAND,
	WIDX_CONSTRUCT_BRIDGE_OR_TUNNEL,
};

static rct_widget window_footpath_widgets[] = {
	{ WWT_FRAME,	0,		0,		105,	0,		380,	0x0FFFFFFFF,	STR_NONE },
	{ WWT_CAPTION,	0,		1,		104,	1,		14,		STR_FOOTPATHS,	STR_WINDOW_TITLE_TIP },
	{ WWT_CLOSEBOX,	0,		93,		103,	2,		13,		STR_CLOSE_X,	STR_CLOSE_WINDOW_TIP },

	// Type group
	{ WWT_GROUPBOX,	0,		3,		102,	17,		71,		STR_TYPE,		STR_NONE },
	{ WWT_FLATBTN,	1,		6,		52,		30,		65,		0xFFFFFFFF,		STR_FOOTPATH_TIP },
	{ WWT_FLATBTN,	1,		53,		99,		30,		65,		0xFFFFFFFF,		STR_QUEUE_LINE_PATH_TIP },

	// Direction group
	{ WWT_GROUPBOX,	0,		3,		102,	75,		151,	STR_DIRECTION,	STR_NONE },
	{ WWT_FLATBTN,	1,		53,		97,		87,		115,	5635,			STR_DIRECTION_TIP },
	{ WWT_FLATBTN,	1,		53,		97,		116,	144,	5636,			STR_DIRECTION_TIP },
	{ WWT_FLATBTN,	1,		8,		52,		116,	144,	5637,			STR_DIRECTION_TIP },
	{ WWT_FLATBTN,	1,		8,		52,		87,		115,	5638,			STR_DIRECTION_TIP },

	// Slope group
	{ WWT_GROUPBOX,	0,		3,		102,	155,	195,	STR_SLOPE,		STR_NONE },
	{ WWT_FLATBTN,	1,		17,		40,		167,	190,	5145,			STR_SLOPE_DOWN_TIP },
	{ WWT_FLATBTN,	1,		41,		64,		167,	190,	5146,			STR_LEVEL_TIP },
	{ WWT_FLATBTN,	1,		65,		88,		167,	190,	5147,			STR_SLOPE_UP_TIP },
	{ WWT_FLATBTN,	1,		8,		97,		202,	291,	0xFFFFFFFF,		STR_CONSTRUCT_THE_SELECTED_FOOTPATH_SECTION_TIP },
	{ WWT_FLATBTN,	1,		30,		75,		295,	318,	5162,			STR_REMOVE_PREVIOUS_FOOTPATH_SECTION_TIP },

	// Mode group
	{ WWT_GROUPBOX,	0,		3,		102,	321,	374,	0xFFFFFFFF,		STR_NONE },
	{ WWT_FLATBTN,	1,		13,		48,		332,	367,	5639,			STR_CONSTRUCT_FOOTPATH_ON_LAND_TIP },
	{ WWT_FLATBTN,	1,		57,		92,		332,	367,	5640,			STR_CONSTRUCT_BRIDGE_OR_TUNNEL_FOOTPATH_TIP },
	{ WIDGETS_END },
};

static void window_footpath_emptysub() { }
static void window_footpath_close();
static void window_footpath_mouseup();
static void window_footpath_mousedown();
static void window_footpath_dropdown();
static void window_footpath_update();
static void window_footpath_toolupdate();
static void window_footpath_tooldown();
static void window_footpath_tooldrag();
static void window_footpath_toolup();
static void window_footpath_invalidate();
static void window_footpath_paint();

static uint32 window_footpath_events[] = {
	window_footpath_close,
	window_footpath_mouseup,
	window_footpath_emptysub,
	window_footpath_mousedown,
	window_footpath_dropdown,
	window_footpath_emptysub,
	window_footpath_update,
	window_footpath_emptysub,
	window_footpath_emptysub,
	window_footpath_toolupdate,
	window_footpath_tooldown,
	window_footpath_tooldrag,
	window_footpath_toolup,
	window_footpath_emptysub,
	window_footpath_emptysub,
	window_footpath_emptysub,
	window_footpath_emptysub,
	window_footpath_emptysub,
	window_footpath_emptysub,
	window_footpath_emptysub,
	window_footpath_emptysub,
	window_footpath_emptysub,
	window_footpath_emptysub,
	window_footpath_emptysub,
	window_footpath_emptysub,
	window_footpath_invalidate,
	window_footpath_paint,
	window_footpath_emptysub
};

sint32 _window_footpath_cost;

static void window_footpath_show_footpath_types_dialog(rct_window *w, rct_widget *widget, int showQueues);

/**
 * 
 *  rct2: 0x006A7C43
 */
void window_footpath_open()
{
	rct_window* window;

	// Check if window is already open
	window = window_bring_to_front_by_id(WC_FOOTPATH, 0);
	if (window != NULL)
		return;

	window = window_create(
		0,
		29,
		106,
		381,
		window_footpath_events,
		WC_FOOTPATH,
		0
	);
	window->widgets = window_footpath_widgets;
	window->enabled_widgets =
		(1 << WIDX_CLOSE) |
		(1 << WIDX_FOOTPATH_TYPE) |
		(1 << WIDX_QUEUELINE_TYPE) |
		(1 << WIDX_DIRECTION_NW) |
		(1 << WIDX_DIRECTION_NE) |
		(1 << WIDX_DIRECTION_SW) |
		(1 << WIDX_DIRECTION_SE) |
		(1 << WIDX_SLOPEDOWN) |
		(1 << WIDX_LEVEL) |
		(1 << WIDX_SLOPEUP) |
		(1 << WIDX_CONSTRUCT) |
		(1 << WIDX_REMOVE) |
		(1 << WIDX_CONSTRUCT_ON_LAND) |
		(1 << WIDX_CONSTRUCT_BRIDGE_OR_TUNNEL);

	window_init_scroll_widgets(window);
	RCT2_CALLPROC_EBPSAFE(0x006EE65A);
	show_gridlines();
	window->colours[0] = 24;
	window->colours[1] = 24;
	window->colours[2] = 24;

	RCT2_CALLPROC_EBPSAFE(0x006EE281);
	RCT2_GLOBAL(0x00F3EF99, uint8) = 0;
	RCT2_CALLPROC_X(0x006EE212, 17, 0, 0, 18, window, 0, 0);
	RCT2_GLOBAL(0x009DE518, uint32) |= (1 << 6);
	RCT2_GLOBAL(0x00F3EF9F, uint8) = 0;
	RCT2_CALLPROC_EBPSAFE(0x006A855C);
}

/**
 * 
 *  rct2: 0x006A852F
 */
static void window_footpath_close()
{
	rct_window *w;

	__asm mov w, esi

	RCT2_CALLPROC_EBPSAFE(0x006A7831);
	RCT2_CALLPROC_X(0x006CB70A, 0, 0, 0, 0, 0, 0, 0);
	RCT2_CALLPROC_EBPSAFE(0x0068AB1B);
	RCT2_GLOBAL(0x009DE58A, uint16) &= ~2;
	window_invalidate_by_id(WC_TOP_TOOLBAR, 0);
	hide_gridlines();
}

/**
 * 
 *  rct2: 0x006A7E92
 */
static void window_footpath_mouseup()
{
	short widgetIndex;
	rct_window *w;

	__asm mov widgetIndex, dx
	__asm mov w, esi

	switch (widgetIndex) {
	case WIDX_CLOSE:
		window_close(w);
		break;
	case WIDX_CONSTRUCT:
		RCT2_CALLPROC_X(0x006A79B7, 0, 0, 0, 0, w, 0, 0);
		break;
	case WIDX_REMOVE:
		RCT2_CALLPROC_X(0x006A7863, 0, 0, 0, 0, w, 0, 0);
		break;
	case WIDX_CONSTRUCT_ON_LAND:
		RCT2_CALLPROC_X(0x006A8072, 0, 0, 0, 0, w, 0, 0);
		break;
	case WIDX_CONSTRUCT_BRIDGE_OR_TUNNEL:
		RCT2_CALLPROC_X(0x006A80C5, 0, 0, 0, 0, w, 0, 0);
		break;
	}
}

/**
 * 
 *  rct2: 0x006A7EC5
 */
static void window_footpath_mousedown()
{
	short widgetIndex;
	rct_window *w;
	rct_widget *widget;

	__asm mov widgetIndex, dx
	__asm mov w, esi
	__asm mov widget, edi

	switch (widgetIndex) {
	case WIDX_FOOTPATH_TYPE:
		window_footpath_show_footpath_types_dialog(w, widget, 0);
		break;
	case WIDX_QUEUELINE_TYPE:
		window_footpath_show_footpath_types_dialog(w, widget, 1);
		break;
	case WIDX_DIRECTION_NW:
		RCT2_CALLPROC_X(0x006A8111, 0, 0, 0, 0, w, 0, 0);
		break;
	case WIDX_DIRECTION_NE:
		RCT2_CALLPROC_X(0x006A8135, 0, 0, 0, 0, w, 0, 0);
		break;
	case WIDX_DIRECTION_SW:
		RCT2_CALLPROC_X(0x006A815C, 0, 0, 0, 0, w, 0, 0);
		break;
	case WIDX_DIRECTION_SE:
		RCT2_CALLPROC_X(0x006A8183, 0, 0, 0, 0, w, 0, 0);
		break;
	case WIDX_SLOPEDOWN:
		RCT2_CALLPROC_X(0x006A81AA, 0, 0, 0, 0, w, 0, 0);
		break;
	case WIDX_LEVEL:
		RCT2_CALLPROC_X(0x006A81C5, 0, 0, 0, 0, w, 0, 0);
		break;
	case WIDX_SLOPEUP:
		RCT2_CALLPROC_X(0x006A81E0, 0, 0, 0, 0, w, 0, 0);
		break;
	}
}

/**
 * 
 *  rct2: 0x006A7F18
 */
static void window_footpath_dropdown()
{
	int i, pathId;
	short dropdownIndex;
	short widgetIndex;
	rct_window *w;
	rct_path_type *pathType;

	__asm mov dropdownIndex, ax
	__asm mov widgetIndex, dx
	__asm mov w, esi

	if (widgetIndex == WIDX_FOOTPATH_TYPE)
		RCT2_GLOBAL(0x00F3EFA2, uint8) = 0;
	else if (widgetIndex == WIDX_QUEUELINE_TYPE)
		RCT2_GLOBAL(0x00F3EFA2, uint8) = 1;
	else
		return;
	
	pathId = dropdownIndex;
	if (pathId == -1) {
		pathId = RCT2_GLOBAL(0x00F3EFA0, sint16);
	} else {
		int flags = 4;
		if (RCT2_GLOBAL(RCT2_ADDRESS_SCREEN_FLAGS, uint8) & 2)
			flags = 0;

		i = -1;
		do {
			i++;
			pathType = RCT2_ADDRESS(0x009ADA14, rct_path_type*)[i];
			if (pathType == (rct_path_type*)-1)
				continue;
			if (pathType->flags & flags)
				continue;
		} while (--pathId >= 0);
		pathId = i;
	}

	RCT2_GLOBAL(0x00F3EFA0, sint16) = pathId;
	RCT2_CALLPROC_EBPSAFE(0x006A7831);
	_window_footpath_cost = 0x80000000;
	window_invalidate(w);
}

/**
 * 
 *  rct2: 0x006A8032
 */
static void window_footpath_toolupdate()
{
	short widgetIndex;
	rct_window *w;

	__asm mov widgetIndex, dx
	__asm mov w, esi

	if (widgetIndex == WIDX_CONSTRUCT_ON_LAND) {
		RCT2_CALLPROC_X(0x006A81FB, 0, 0, 0, 0, w, 0, 0);
	} else if (widgetIndex == WIDX_CONSTRUCT_BRIDGE_OR_TUNNEL) {
		RCT2_CALLPROC_X(0x006A8388, 0, 0, 0, 0, w, 0, 0);
	}
}

/**
 * 
 *  rct2: 0x006A8047
 */
static void window_footpath_tooldown()
{
	short widgetIndex;
	rct_window *w;

	__asm mov widgetIndex, dx
	__asm mov w, esi

	if (widgetIndex == WIDX_CONSTRUCT_ON_LAND) {
		RCT2_CALLPROC_X(0x006A82C5, 0, 0, 0, 0, w, 0, 0);
	} else if (widgetIndex == WIDX_CONSTRUCT_BRIDGE_OR_TUNNEL) {
		RCT2_CALLPROC_X(0x006A840F, 0, 0, 0, 0, w, 0, 0);
	}
}

/**
 * 
 *  rct2: 0x006A8067
 */
static void window_footpath_tooldrag()
{
	short widgetIndex;
	rct_window *w;

	__asm mov widgetIndex, dx
	__asm mov w, esi

	if (widgetIndex == WIDX_CONSTRUCT_ON_LAND) {
		RCT2_CALLPROC_X(0x006A82C5, 0, 0, 0, 0, w, 0, 0);
	}
}

/**
 * 
 *  rct2: 0x006A8066
 */
static void window_footpath_toolup()
{
	short widgetIndex;
	rct_window *w;

	__asm mov widgetIndex, dx
	__asm mov w, esi

	if (widgetIndex == WIDX_CONSTRUCT_ON_LAND) {
		RCT2_CALLPROC_X(0x006A8380, 0, 0, 0, 0, w, 0, 0);
	}
}

/**
 * 
 *  rct2: 0x006A84BB
 */
static void window_footpath_update()
{
	rct_window *w;

	__asm mov w, esi

	// Invalidate construct button
	widget_invalidate(WC_FOOTPATH, 0, WIDX_CONSTRUCT);

	RCT2_CALLPROC_EBPSAFE(0x006A7760);

	// Check tool
	if (RCT2_GLOBAL(0x00F3EF99, uint8) == 0) {
		if (!(RCT2_GLOBAL(0x009DE518, uint32) & (1 << 3)))
			window_close(w);
		if (RCT2_GLOBAL(RCT2_ADDRESS_TOOL_WINDOWCLASS, rct_windowclass) != WC_FOOTPATH)
			window_close(w);
		if (RCT2_GLOBAL(RCT2_ADDRESS_TOOL_WIDGETINDEX, uint16) != WIDX_CONSTRUCT_ON_LAND)
			window_close(w);
	} else if (RCT2_GLOBAL(0x00F3EF99, uint8) == 1) {
		if (!(RCT2_GLOBAL(0x009DE518, uint32) & (1 << 3)))
			window_close(w);
		if (RCT2_GLOBAL(RCT2_ADDRESS_TOOL_WINDOWCLASS, rct_windowclass) != WC_FOOTPATH)
			window_close(w);
		if (RCT2_GLOBAL(RCT2_ADDRESS_TOOL_WIDGETINDEX, uint16) != WIDX_CONSTRUCT_BRIDGE_OR_TUNNEL)
			window_close(w);
	}
}

/**
 * 
 *  rct2: 0x006A7D1C
 */
static void window_footpath_invalidate()
{
	int selectedPath;
	rct_path_type *pathType;
	rct_window *w;

	__asm mov w, esi
	
	// Press / unpress footpath and queue type buttons
	w->pressed_widgets &= ~(1 << WIDX_FOOTPATH_TYPE);
	w->pressed_widgets &= ~(1 << WIDX_QUEUELINE_TYPE);
	w->pressed_widgets |= RCT2_GLOBAL(0x00F3EFA2, uint8) == 0 ?
		(1 << WIDX_FOOTPATH_TYPE) :
		(1 << WIDX_QUEUELINE_TYPE);

	// Enable / disable construct button
	window_footpath_widgets[WIDX_CONSTRUCT].type = RCT2_GLOBAL(0x00F3EF99, uint8) == 0 ? WWT_EMPTY : WWT_IMGBTN;

	// Set footpath and queue type button images
	selectedPath = RCT2_GLOBAL(0x00F3EFA0, uint16);
	pathType = RCT2_ADDRESS(0x009ADA14, rct_path_type*)[selectedPath];

	int pathImage = 71 + pathType->image;
	window_footpath_widgets[WIDX_FOOTPATH_TYPE].image = pathImage;
	window_footpath_widgets[WIDX_QUEUELINE_TYPE].image = pathImage + 1;
	window_footpath_widgets[WIDX_QUEUELINE_TYPE].type = WWT_FLATBTN;

	// Disable queue in if in editor
	if (RCT2_GLOBAL(RCT2_ADDRESS_SCREEN_FLAGS, uint8) & 2)
		window_footpath_widgets[WIDX_QUEUELINE_TYPE].type = WWT_EMPTY;
}

/**
 * 
 *  rct2: 0x006A7D8B
 */
static void window_footpath_paint()
{
	int x, y, image, selectedPath;
	rct_path_type *pathType;
	rct_window *w;
	rct_drawpixelinfo *dpi;

	__asm mov w, esi
	__asm mov dpi, edi

	window_draw_widgets(w, dpi);

	if (!(w->disabled_widgets & (1 << WIDX_CONSTRUCT))) {
		// Get construction image
		image = (RCT2_GLOBAL(0x00F3EF90, uint8) + RCT2_GLOBAL(RCT2_ADDRESS_CURRENT_ROTATION, uint32)) % 4;
		if (RCT2_GLOBAL(0x00F3EF91, uint8) == 2)
			image += 4;
		else if (RCT2_GLOBAL(0x00F3EF91, uint8) == 6)
			image += 8;
		image = RCT2_ADDRESS(0x0098D7E0, uint8)[image];

		selectedPath = RCT2_GLOBAL(0x00F3EFA0, uint16);
		pathType = RCT2_ADDRESS(0x009ADA14, rct_path_type*)[selectedPath];
		image += pathType->image;
		if (RCT2_GLOBAL(0x00F3EFA2, uint8) != 0)
			image += 51;

		// Draw construction image
		x = w->x + (window_footpath_widgets[WIDX_CONSTRUCT].left + window_footpath_widgets[WIDX_CONSTRUCT].right) / 2;
		y = w->y + window_footpath_widgets[WIDX_CONSTRUCT].bottom - 60;
		gfx_draw_sprite(dpi, image, x, y);

		// Draw build this... label
		x = w->x + (window_footpath_widgets[WIDX_CONSTRUCT].left + window_footpath_widgets[WIDX_CONSTRUCT].right) / 2;
		y = w->y + window_footpath_widgets[WIDX_CONSTRUCT].bottom - 23;
		gfx_draw_string_centred(dpi, STR_BUILD_THIS, x, y, 0, 0);
	}

	// Draw cost
	x = w->x + (window_footpath_widgets[WIDX_CONSTRUCT].left + window_footpath_widgets[WIDX_CONSTRUCT].right) / 2;
	y = w->y + window_footpath_widgets[WIDX_CONSTRUCT].bottom - 12;
	if (_window_footpath_cost != 0x80000000)
		if (!(RCT2_GLOBAL(RCT2_ADDRESS_GAME_FLAGS, uint32) & GAME_FLAGS_NO_MONEY))
			gfx_draw_string_centred(dpi, STR_COST_LABEL, x, y, 0, &_window_footpath_cost);
}

/**
 * 
 *  rct2: 0x006A7F88
 */
static void window_footpath_show_footpath_types_dialog(rct_window *w, rct_widget *widget, int showQueues)
{
	int i, flags, numPathTypes, image;
	rct_path_type *pathType;

	numPathTypes = 0;
	flags = 4;
	if (RCT2_GLOBAL(RCT2_ADDRESS_SCREEN_FLAGS, uint8) & 2)
		flags = 0;

	for (i = 0; i < 16; i++) {
		pathType = RCT2_ADDRESS(0x009ADA14, rct_path_type*)[i];
		if (pathType == (rct_path_type*)-1)
			continue;
		if (pathType->flags & flags)
			continue;

		image = pathType->image + 71;
		if (showQueues)
			image++;

		gDropdownItemsFormat[numPathTypes] = -1;
		gDropdownItemsArgs[numPathTypes] = image;
		numPathTypes++;
	}

	window_dropdown_show_image(
		w->x + widget->left, w->y + widget->top, widget->bottom - widget->top + 1,
		w->colours[1],
		0,
		numPathTypes,
		47,
		36,
		gAppropriateImageDropdownItemsPerRow[numPathTypes]
	);
}