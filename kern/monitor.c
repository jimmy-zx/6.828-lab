// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>
#include <inc/types.h>

#include <kern/pmap.h>
#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "vmlst", "List the mappings and permissions of a range of VAs", mon_vmlst },
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	uint32_t *ebp;
	struct Eipdebuginfo info;
	
	cprintf("Stack backtrace:\n");
	ebp = (uint32_t *)read_ebp();
	/* Stack frame: ^Top
	 * *ebp = base pointer of caller <- ebp
	 *  eip = return address <- ebp + 1
	 * arg1                  <- ebp + 2
	 * arg2                  <- ebp + 3
	 * ...
	 */
	do {
		cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n",
			(uint32_t)ebp, *(ebp + 1),
			*(ebp + 2), *(ebp + 3), *(ebp + 4), *(ebp + 5), *(ebp + 6));
		debuginfo_eip(*(ebp + 1), &info);
		cprintf("         %s:%d: %.*s+%d\n",
			info.eip_file, info.eip_line,
			info.eip_fn_namelen, info.eip_fn_name, *(ebp + 1) - info.eip_fn_addr);
		ebp = (uint32_t *)*ebp;
	} while (ebp != NULL);
	return 0;
}

int
mon_vmlst(int argc, char **argv, struct Trapframe *tf)
{
	uintptr_t begin, end, cur;
	pte_t *pte;

	if (argc == 3) {
		begin = ROUNDDOWN(strtol(argv[1], NULL, 16), PGSIZE);
		end = ROUNDUP(strtol(argv[2], NULL, 16), PGSIZE);
		cprintf("vmlst: %p - %p\n", begin, end);
		for (cur = begin; cur < end; cur+= PGSIZE) {
			pte = pgdir_walk(kern_pgdir, (void *)cur, 0);
			if (pte == NULL) {
				continue;
			} else {
				cprintf("%08p -> %08p %s %s\n",
					cur, PTE_ADDR(*pte),
					*pte & PTE_W ? "RW" : "RO",
					*pte & PTE_U ? "U" : "S");
			}
		}
	} else {
		cprintf("Usage: vmlst [start] [end]\n");
	}
	return 0;
}


/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
