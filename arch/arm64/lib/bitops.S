/*
 * Based on arch/arm/lib/bitops.h
 *
 * Copyright (C) 2013 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/lse.h>

/*
 * x0: bits 5:0  bit offset
 *     bits 31:6 word offset
 * x1: address
 */
	.macro	bitop, name, llsc, lse
ENTRY(	\name	)
	nop
	ret
ENDPROC(\name	)
	.endm

	.macro	testop, name, llsc, lse
ENTRY(	\name	)
	nop
	ret
ENDPROC(\name	)
	.endm

/*
 * Atomic bit operations.
 */
	bitop	change_bit, eor, steor
	bitop	clear_bit, bic, stclr
	bitop	set_bit, orr, stset

	testop	test_and_change_bit, eor, ldeoral
	testop	test_and_clear_bit, bic, ldclral
	testop	test_and_set_bit, orr, ldsetal
