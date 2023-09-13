/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2016-2019 The Linux Foundation. All rights reserved.
 */

#ifndef __PMIC_VOTER_H
#define __PMIC_VOTER_H

#include <linux/mutex.h>

struct votable;

enum votable_type {
	VOTE_MIN,
	VOTE_MAX,
	VOTE_SET_ANY,
	NUM_VOTABLE_TYPES,
};

bool oplus_is_client_vote_enabled(struct votable *votable,
				  const char *client_str);
bool oplus_is_client_vote_enabled_locked(struct votable *votable,
					 const char *client_str);
bool oplus_is_override_vote_enabled(struct votable *votable);
bool oplus_is_override_vote_enabled_locked(struct votable *votable);
int oplus_get_client_vote(struct votable *votable, const char *client_str);
int oplus_get_client_vote_locked(struct votable *votable,
				 const char *client_str);
int oplus_get_effective_result(struct votable *votable);
int oplus_get_effective_result_locked(struct votable *votable);
const char *oplus_get_effective_client(struct votable *votable);
const char *oplus_get_effective_client_locked(struct votable *votable);
int oplus_vote(struct votable *votable, const char *client_str, bool state,
	       int val, bool step);
int oplus_vote_override(struct votable *votable, const char *override_client,
			bool state, int val, bool step);
int oplus_rerun_election(struct votable *votable, bool step);
struct votable *oplus_find_votable(const char *name);
struct votable *
oplus_create_votable(const char *name, int votable_type,
		     int (*callback)(struct votable *votable, void *data,
				     int effective_result,
				     const char *effective_client, bool step),
		     void *data);
void oplus_destroy_votable(struct votable *votable);
void oplus_lock_votable(struct votable *votable);
void oplus_unlock_votable(struct votable *votable);

#endif /* __PMIC_VOTER_H */
