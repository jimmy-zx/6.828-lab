// program to cause a breakpoint trap

#include <inc/lib.h>

int a;

void
umain(int argc, char **argv)
{
	a = 0;
	asm volatile("int $3");
	a = 1;
}

