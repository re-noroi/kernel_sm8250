// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/slab.h>

#include "adreno.h"
#include "adreno_cp_parser.h"
#include "adreno_pm4types.h"

#define MAX_IB_OBJS 1000
#define NUM_SET_DRAW_GROUPS 32

struct set_draw_state {
	uint64_t cmd_stream_addr;
	uint64_t cmd_stream_dwords;
};

/* List of variables used when parsing an IB */
struct ib_parser_variables {
	/* List of registers containing addresses and their sizes */
	unsigned int cp_addr_regs[ADRENO_CP_ADDR_MAX];
	/* 32 groups of command streams in set draw state packets */
	struct set_draw_state set_draw_groups[NUM_SET_DRAW_GROUPS];
};

/*
 * adreno_ib_create_object_list() - Find all the memory objects in IB
 * @device: The device pointer on which the IB executes
 * @process: The process in which the IB and all contained objects are mapped
 * @gpuaddr: The gpu address of the IB
 * @dwords: Size of ib in dwords
 * @ib2base: Base address of active IB2
 * @ib_obj_list: The list in which the IB and the objects in it are added.
 *
 * Find all the memory objects that an IB needs for execution and place
 * them in a list including the IB.
 * Returns the ib object list. On success 0 is returned, on failure error
 * code is returned along with number of objects that was saved before
 * error occurred. If no objects found then the list pointer is set to
 * NULL.
 */
int adreno_ib_create_object_list(struct kgsl_device *device,
		struct kgsl_process_private *process,
		uint64_t gpuaddr, uint64_t dwords, uint64_t ib2base,
		struct adreno_ib_object_list **out_ib_obj_list)
{
	struct adreno_ib_object_list *ib_obj_list;

	if (!out_ib_obj_list)
		return -EINVAL;

	*out_ib_obj_list = NULL;

	ib_obj_list = kzalloc(sizeof(*ib_obj_list), GFP_KERNEL);
	if (!ib_obj_list)
		return -ENOMEM;

	ib_obj_list->obj_list = vmalloc(MAX_IB_OBJS *
					sizeof(struct adreno_ib_object));

	if (!ib_obj_list->obj_list) {
		kfree(ib_obj_list);
		return -ENOMEM;
	}

	/* Even if there was an error return the remaining objects found */
	if (ib_obj_list->num_objs)
		*out_ib_obj_list = ib_obj_list;

	return 0;
}

/*
 * adreno_ib_destroy_obj_list() - Destroy an ib object list
 * @ib_obj_list: List to destroy
 *
 * Free up all resources used by an ib_obj_list
 */
void adreno_ib_destroy_obj_list(struct adreno_ib_object_list *ib_obj_list)
{
	int i;

	if (!ib_obj_list)
		return;

	for (i = 0; i < ib_obj_list->num_objs; i++) {
		if (ib_obj_list->obj_list[i].entry)
			kgsl_mem_entry_put(ib_obj_list->obj_list[i].entry);
	}
	vfree(ib_obj_list->obj_list);
	kfree(ib_obj_list);
}
