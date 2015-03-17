#ifndef TRAILERMATIC_H__
#define TRAILERMATIC_H__

/*
 * Copyright (C) 2008 Frank Aurich 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifdef MEMWATCH
	#include "memwatch.h"
#endif
#define AM_DEFAULT_INTERVAL			30

#include <stdint.h>

#include "feed_item.h"
#include "rss_feed.h"
#include "filters.h"

/** \cond */
struct auto_handle {
	char *statefile;
	char *download_folder;
	char *prowl_key;
	rss_feeds   feeds;
	am_filters  filters;
	simple_list downloads;
	int8_t      rpc_version;
	uint8_t     prowl_key_valid;
	uint16_t    max_bucket_items;
	uint8_t     bucket_changed;
	uint8_t     check_interval;
	uint8_t     match_only;
};
/** \endcond */

typedef struct auto_handle auto_handle;

/* uint8_t am_get_verbose(void); */

#endif /* TRAILERMATIC_H__ */
