/*  Sarien - A Sierra AGI resource interpreter engine
 *  Copyright (C) 1999-2001 Stuart George and Claudio Matsuoka
 *  
 *  $Id$
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; see docs/COPYING for further details.
 */

/*
 * Based on the Amiga graphics output for v2600 Atari 2600 emulator,
 * version 2.2 (September 4, 1997) written by Matthew Stroup
 */

#include <stdlib.h>
#include <string.h>
#include <clib/exec_protos.h>
#include <clib/graphics_protos.h>
#include <clib/intuition_protos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/view.h>
#include <exec/memory.h>
#include <hardware/cia.h>
#include "sarien.h"
#include "graphics.h"

#define NUMCOLS 32
#define CIAAPRA 0xBFE001
#define CIAA	0xBFE001


extern struct gfx_driver *gfx;
extern struct sarien_options opt;


static int	amiga_init_vidmode	(void);
static int	amiga_deinit_vidmode	(void);
static void	amiga_put_block		(int, int, int, int);
static void	amiga_put_pixels	(int, int, int, UINT8 *);
static void	amiga_timer		(void);
static int	amiga_get_key		(void);
static int	amiga_keypress		(void);


static struct gfx_driver gfx_amiga = {
	amiga_init_vidmode,
	amiga_deinit_vidmode,
	amiga_put_block,
	amiga_put_pixels,
	amiga_timer,
	amiga_keypress,
	amiga_get_key
};

static struct Window *win;
static struct RastPort *rp;
static UBYTE *vscreen;
static UBYTE colors[NUMCOLS];
static struct CIA *ciamou = (struct CIA *)CIAAPRA;
static struct CIA *cia = (struct CIA *)CIAA;

extern struct GfxBase *GfxBase;
extern struct IntuitionBase *IntuitionBase;



#define KEY_QUEUE_SIZE 16

static int key_queue[KEY_QUEUE_SIZE];
static int key_queue_start = 0;
static int key_queue_end = 0;

#define key_enqueue(k) do { key_queue[key_queue_end++] = (k); \
	key_queue_end %= KEY_QUEUE_SIZE; } while (0)
#define key_dequeue(k) do { (k) = key_queue[key_queue_start++]; \
	key_queue_start %= KEY_QUEUE_SIZE; } while (0)

static int keymap[256] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '\'', 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	' ',		/* 0x40 */
	0, 0,
	KEY_ENTER,	/* 0x43 */
	KEY_ENTER,	/* 0x44 */
	KEY_ESC,	/* 0x45 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', 0, 0, 0, 0, 0, 0,
	'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', 0, 0, 0, 0, 0, 0,
	0, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static void process_events ()
{
	struct IntuiMessage *message;
	UINT8 key;

	message = (struct IntuiMessage *)GetMsg (win->UserPort);
	if (message) {
		switch (message->Class) {
		case IDCMP_CLOSEWINDOW:
			amiga_deinit_vidmode ();
			exit (0);
		}
	}

	mouse.x = win->MouseX;
	mouse.y = win->MouseY;
	mouse.button = !(ciamou->ciapra & 0x0040);

        key = cia->ciasdr ^ 0xFF;
        key = key & 0x01 ? (key >> 1) + 0x80 : key >> 1;
        /* if (key == 0x45) exit(0); */

	_D ("%02x %02x %c %c", key, keymap[key], key, keymap[key]);
	if (~key & 0x80)
		key_enqueue (keymap[key]);
}

int init_machine (int argc, char **argv)
{
	gfx = &gfx_amiga;
	return err_OK;
}

int deinit_machine ()
{
	return err_OK;
}

static void amiga_timer ()
{
#if 0
	struct timeval tv;
	struct timezone tz;
	static double msec = 0.0;
	double m;
	
	gettimeofday (&tv, &tz);
	m = 1000.0 * tv.tv_sec + tv.tv_usec / 1000.0;

	while (m - msec < 42) {
		usleep (5000);
		gettimeofday (&tv, &tz);
		m = 1000.0 * tv.tv_sec + tv.tv_usec / 1000.0;
	}
	msec = m; 
#endif

	process_events ();
}

static int amiga_init_vidmode ()
{
	int i;

	/* Make sure necessary ROM libraries are open */

	IntuitionBase = (struct IntuitionBase *)
		OpenLibrary ("intuition.library", 36L);
	if (IntuitionBase == NULL)
		return -1;

	GfxBase = (struct GfxBase *) OpenLibrary ("graphics.library", 36L);
	if (GfxBase == NULL)
		return -1;

	vscreen = (UBYTE *)AllocMem (GFX_WIDTH * GFX_HEIGHT,
		MEMF_PUBLIC | MEMF_CLEAR);

	win = OpenWindowTags (NULL,
		WA_Title, "Sarien " ##VERSION,
		WA_AutoAdjust, TRUE,
		WA_InnerWidth, GFX_WIDTH, 
		WA_InnerHeight, GFX_HEIGHT,
		WA_DragBar, TRUE,
		WA_CloseGadget, TRUE,
		WA_DepthGadget,TRUE,
		WA_SimpleRefresh, TRUE,
		WA_IDCMP, IDCMP_CLOSEWINDOW,
		TAG_DONE);
	if (win == NULL)
		return -1;

	rp = win->RPort;

	for (i = 0; i < NUMCOLS; i++) {
		colors[i] = ObtainBestPen (win->WScreen->ViewPort.ColorMap,
			(UINT32)palette[i * 3 + 0] << 26,
			(UINT32)palette[i * 3 + 1] << 26,
			(UINT32)palette[i * 3 + 2] << 26,
			OBP_Precision,PRECISION_EXACT,
			TAG_DONE);
	}

	/* clear screen */
	memset (vscreen, colors[0], GFX_WIDTH * GFX_HEIGHT);

	return 0;
}

static int amiga_deinit_vidmode ()
{
	CloseWindow (win);
	FreeMem (vscreen, GFX_WIDTH * GFX_HEIGHT);
	CloseLibrary ((struct Library *)GfxBase);
	CloseLibrary ((struct Library *)IntuitionBase);

	return 0;
}


/* blit a block onto the screen */
static void amiga_put_block (int x1, int y1, int x2, int y2)
{
	if (x1 >= GFX_WIDTH)  x1 = GFX_WIDTH  - 1;
	if (y1 >= GFX_HEIGHT) y1 = GFX_HEIGHT - 1;
	if (x2 >= GFX_WIDTH)  x2 = GFX_WIDTH  - 1;
	if (y2 >= GFX_HEIGHT) y2 = GFX_HEIGHT - 1;

	WriteChunkyPixels (rp,
		win->BorderLeft + x1, win->BorderTop + y1,
		win->BorderLeft + x2, win->BorderTop + y2,
		vscreen + y1 * GFX_WIDTH + x1, GFX_WIDTH);
}


static void amiga_put_pixels (int x, int y, int w, UINT8 *p)
{
	UINT8 *v = vscreen + y * GFX_WIDTH + x;
	while (w--) { *v++ = colors[*p++]; }
}


static int amiga_keypress ()
{
	process_events ();
	return key_queue_start != key_queue_end;
}


static int amiga_get_key ()
{
	UINT16 k;

	while (key_queue_start == key_queue_end)	/* block */
		amiga_timer ();

	key_dequeue(k);

	return k;
}

