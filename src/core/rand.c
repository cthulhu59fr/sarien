/*  Sarien - A Sierra AGI resource interpreter engine
 *  Copyright (C) 1999-2001 Stuart George and Claudio Matsuoka
 *  
 *  $Id$
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; see docs/COPYING for further details.
 */

#include <time.h>
#include "sarien.h"
#include "rand.h"

SINT32 rnd_seed;

void set_rnd_seed(void)
{
	set_xrnd_seed(time(NULL));
}

SINT32 get_rnd_seed(void)
{
	return rnd_seed;
}

void set_xrnd_seed(SINT32 seedval)
{
	rnd_seed = (seedval % (RNG_M-1)) + 1;
}

SINT32 xrnd(void)
{
	SINT32 low, high, test;

	high = rnd_seed / RNG_Q;
	low = rnd_seed % RNG_Q;
	test = RNG_A * low - RNG_R * high;

	return rnd_seed = test > 0 ? test : test + RNG_M;
}

SINT32 rnd(SINT32 maxrnd)
{
	return maxrnd ? xrnd() % maxrnd : xrnd();
}

