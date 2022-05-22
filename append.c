/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2022 Cameron Katri.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY CAMERON KATRI AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL CAMERON KATRI OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "trustcache.h"
#include "uuid/uuid.h"

int
tcappend(int argc, char **argv)
{
	int keepuuid = 0;
	uuid_t uuid;
	const char *errstr = NULL;
	uint8_t flags = 0;

	int ch;
	while ((ch = getopt(argc, argv, "u:f:")) != -1) {
		switch (ch) {
			case 'u':
				if (strlen(optarg) == 1 && *optarg == '0') {
					keepuuid = 1;
				} else {
					if (uuid_parse(optarg, uuid) != 0) {
						fprintf(stderr, "Failed to parse %s as a UUID\n", optarg);
					} else
						keepuuid = 2;
				}
				break;
			case 'f':
				flags = strtonum(optarg, 0, UINT8_MAX, &errstr);
				if (errstr != NULL) {
					fprintf(stderr, "flag number is %s: %s\n", errstr, optarg);
					exit(1);
				}
				break;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 2)
		return -1;

	FILE *f = NULL;
	struct trust_cache cache = opentrustcache(argv[0]);
	struct trust_cache append = {
		.version = cache.version,
		.num_entries = 0
	};

	for (int i = 1; i < argc; i++) {
		append = cache_from_tree(argv[i], cache.version);
		if (append.version == 0) {
			if ((cache.hashes = realloc(cache.hashes, sizeof(trust_cache_hash0) *
							(cache.num_entries + append.num_entries))) == NULL)
				exit(1);
			for (uint32_t j = 0; j < append.num_entries; j++) {
				memcpy(cache.hashes[cache.num_entries + j], append.hashes[j], CS_CDHASH_LEN);
			}
		} else if (append.version == 1) {
			if ((cache.entries = realloc(cache.entries, sizeof(struct trust_cache_entry1) *
							(cache.num_entries + append.num_entries))) == NULL)
				exit(1);
			for (uint32_t j = 0; j < append.num_entries; j++) {
				cache.entries[cache.num_entries + j].hash_type = append.entries[j].hash_type;
				cache.entries[cache.num_entries + j].flags = flags != 0 ? flags : append.entries[j].flags;
				memcpy(cache.entries[cache.num_entries + j].cdhash, append.entries[j].cdhash, CS_CDHASH_LEN);
			}
		}
		free(append.hashes);
		cache.num_entries += append.num_entries;
	}

	if (cache.version == 1)
		mergesort(cache.entries, cache.num_entries, sizeof(*cache.entries), ent_cmp);
	else if (cache.version == 0)
		mergesort(cache.hashes, cache.num_entries, sizeof(*cache.hashes), hash_cmp);

	switch (keepuuid) {
		case 0:
			uuid_generate(cache.uuid);
			break;
		case 1:
			break;
		case 2:
			uuid_copy(cache.uuid, uuid);
			break;
	}

	if ((f = fopen(argv[0], "wb")) == NULL) {
		fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
		return 1;
	}

	cache.version = htole32(cache.version);
	cache.num_entries = htole32(cache.num_entries);
	fwrite(&cache, sizeof(struct trust_cache) - sizeof(struct trust_cache_entry1*), 1, f);
	cache.version = le32toh(cache.version);
	cache.num_entries = le32toh(cache.num_entries);

	for (uint32_t i = 0; i < cache.num_entries; i++) {
		if (cache.version == 0)
			fwrite(&cache.hashes[i], sizeof(trust_cache_hash0), 1, f);
		else if (cache.version == 1)
			fwrite(&cache.entries[i], sizeof(struct trust_cache_entry1), 1, f);
	}

	return 0;
}