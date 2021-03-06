/* $Id$
 * $Name$
 * $ProjectName$
 */

/**
 * \file downloads.c
 *
 * Parse configuration file.
 */

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


#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>

#include "list.h"
#include "utils.h"
#include "output.h"

#ifdef MEMWATCH
	#include "memwatch.h"
#endif


static uint8_t bucket_hasURL(const char *url, NODE *head) {
	NODE *p = head;
	char *x;

	while (p != NULL) {
		x = (char*)p->data;
		if(strcmp(x, url) == 0) {
			return 1;
		}
		p = p->next;
	}
	return 0;
}


/** \brief Checks if a file has been downloaded before
 *
 * \param bucket Bucket list that stores the URLS of previously downloaded files
 * \param url URL of the file that should be checked
 * \return 0 if it's a new file, 1 if it has been downloaded before
 */

uint8_t has_been_downloaded(const simple_list bucket, const char *url) {
	return bucket_hasURL(url, bucket);
}

/** \brief add new item to bucket list
 *
 * \param[in] identifier Unique identifier for a bucket item (e.g. a URL)
 * \param[in,out] head pointer to the bucket list
 * \param[in] maxBucketItems number of maximum items in bucket list
 * \return always returns 0
 *
 * The size of the provided bucket list is kept to maxBucketItems.
 * If it gets larger than the specified value, the oldest element is removed from the list.
 */
int addToBucket(const char* identifier, NODE **head, const int maxBucketItems) {

	addToHead(am_strdup(identifier), head);
	if(maxBucketItems > 0 && listCount(*head) > (uint32_t)maxBucketItems) {
		dbg_printf(P_INFO2, "[add_to_bucket] bucket gets too large, deleting last item...\n");
		removeLast(*head, NULL);
	}
	return 0;
}
