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
 * Used for locating shader objects. This array holds the unit size of shader
 * objects based on type and block of shader. The type can be 0 or 1 hence there
 * are 2 columns and block can be 0-7 hence 7 rows.
 */
static int load_state_unit_sizes[7][2] = {
	{ 2, 4 },
	{ 0, 1 },
	{ 2, 4 },
	{ 0, 1 },
	{ 8, 2 },
	{ 8, 2 },
	{ 8, 2 },
};

/*
 * ib_save_mip_addresses() - Find mip addresses
 * @pkt: Pointer to the packet in IB
 * @process: The process in which IB is mapped
 * @ib_obj_list: List in which any objects found are added
 *
 * Returns 0 on success else error code
 */
static int ib_save_mip_addresses(unsigned int *pkt,
		struct kgsl_process_private *process,
		struct adreno_ib_object_list *ib_obj_list)
{
	int ret = 0;
	int num_levels = (pkt[1] >> 22) & 0x03FF;
	int i;
	unsigned int *hostptr;
	struct kgsl_mem_entry *ent;
	unsigned int block, type;
	int unitsize = 0;

	block = (pkt[1] >> 19) & 0x07;
	type = pkt[2] & 0x03;

	if (type == 0)
		unitsize = load_state_unit_sizes[block][0];
	else
		unitsize = load_state_unit_sizes[block][1];

	if (3 == block && 1 == type) {
		uint64_t gpuaddr = pkt[2] & 0xFFFFFFFC;
		uint64_t size = (num_levels * unitsize) << 2;

		ent = kgsl_sharedmem_find(process, gpuaddr);
		if (ent == NULL)
			return 0;

		if (!kgsl_gpuaddr_in_memdesc(&ent->memdesc,
			gpuaddr, size)) {
			kgsl_mem_entry_put(ent);
			return 0;
		}

		kgsl_memdesc_unmap(&ent->memdesc);
		kgsl_mem_entry_put(ent);
	}
	return ret;
}

/*
 * ib_add_type0_entries() - Add memory objects to list
 * @device: The device on which the IB will execute
 * @process: The process in which IB is mapped
 * @ib_obj_list: The list of gpu objects
 * @ib_parse_vars: addresses ranges found in type0 packets
 *
 * Add memory objects to given list that are found in type0 packets
 * Returns 0 on success else 0
 */
static int ib_add_type0_entries(struct kgsl_device *device,
	struct kgsl_process_private *process,
	struct adreno_ib_object_list *ib_obj_list,
	struct ib_parser_variables *ib_parse_vars)
{
	int ret = 0;
	int i;
	int vfd_end;
	unsigned int mask;
	/* First up the visiblity stream buffer */
	mask = 0xFFFFFFFF;
	for (i = ADRENO_CP_ADDR_VSC_PIPE_DATA_ADDRESS_0;
		i < ADRENO_CP_ADDR_VSC_PIPE_DATA_LENGTH_7; i++) {
		if (ib_parse_vars->cp_addr_regs[i]) {
			ib_parse_vars->cp_addr_regs[i] = 0;
			ib_parse_vars->cp_addr_regs[i + 1] = 0;
			i++;
		}
	}

	vfd_end = ADRENO_CP_ADDR_VFD_FETCH_INSTR_1_15;
	for (i = ADRENO_CP_ADDR_VFD_FETCH_INSTR_1_0;
		i <= vfd_end; i++) {
		if (ib_parse_vars->cp_addr_regs[i]) {
			ib_parse_vars->cp_addr_regs[i] = 0;
		}
	}

	if (ib_parse_vars->cp_addr_regs[ADRENO_CP_ADDR_VSC_SIZE_ADDRESS]) {
		ib_parse_vars->cp_addr_regs[
			ADRENO_CP_ADDR_VSC_SIZE_ADDRESS] = 0;
	}
	mask = 0xFFFFFFE0;
	for (i = ADRENO_CP_ADDR_SP_VS_PVT_MEM_ADDR;
		i <= ADRENO_CP_ADDR_SP_FS_OBJ_START_REG; i++) {
		ib_parse_vars->cp_addr_regs[i] = 0;
	}
	return ret;
}

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
	int ret = 0;
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
