/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

.altmacro	/* https://stackoverflow.com/questions/48159005/for-loop-macro-preprocessor-for-assembly-files-in-gcc */

.macro	insertv number
	.section .text
	TRAPHANDLER(vector\number,\number)
	.section .data
	.long vector\number
.endm

.macro	insertv_noec number
	.section .text
	TRAPHANDLER_NOEC(vector\number,\number)
	.section .data
	.long vector\number
.endm

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */

.data
.globl	vectors
vectors:
	insertv_noec	T_DIVIDE
	insertv_noec 	T_DEBUG
	insertv_noec 	T_NMI
	insertv_noec 	T_BRKPT
	insertv_noec	T_OFLOW
	insertv_noec 	T_BOUND
	insertv_noec 	T_ILLOP
	insertv_noec 	T_DEVICE
	insertv		T_DBLFLT
	insertv_noec	9	/* T_COPROC */
	insertv		T_TSS
	insertv		T_SEGNP
	insertv		T_STACK
	insertv		T_GPFLT
	insertv		T_PGFLT
	insertv_noec	15	/* T_RES */
	insertv_noec	T_FPERR
	insertv		T_ALIGN
	insertv_noec	T_MCHK
	insertv_noec	T_SIMDERR
	insertv_noec	20	/* #VE */
	insertv		21	/* #CP */
	/* vectors 22-31 reserved, assuming noec */
	.set	i,22
	.rept	10
		insertv_noec %i
		.set i,i+1
	.endr
	/* vectors 32-255 are user defined interrupts, noec */
	.set	i,32
	.rept	224
		insertv_noec %i
		.set i,i+1
	.endr
.globl	vectors_end
vectors_end:


/*
 * Lab 3: Your code here for _alltraps
 */

.text
_alltraps:
	/* Step 1: construct struct Trapframe */
	pushl %ds
	pushl %es
	pushal
	/* Step 2: load GD_KD into %ds and %es */
	movw $GD_KD,%ax
	movw %ax,%ds
	movw %ax,%es
	pushl %esp	/* Step 3 */
	call trap	/* Step 4 */
