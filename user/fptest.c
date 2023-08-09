#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	assert(_ipow(10, 0) == 1);
	assert(_ipow(10, 5) == 100000);
	assert(_ipow(10, 6) == 1000000);

	register double f1 = 15. / 4.;

	sys_yield();

	// should print 3.750000
	cprintf("%lf\n", f1);
}
