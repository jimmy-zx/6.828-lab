# Homework: xv6 locking

## Don't do this

The kernel will panic as [the lock is already been held by mycpu()](https://github.com/mit-pdos/xv6-public/blob/eeb7b415dbcb12cc362d0783e41c3d1f44066b17/spinlock.c#L28C9-L28C9).

## Interrupts in ide.c

If interrupts is enabled, `ideintr()` will try to acquire the lock due to ide
interrupts (caused by `insl` instruction), and there will be a dead lock.

## Interrupts in file.c

There are no interrupt handlers that acquires `ftable.lock`, therefore most likely
the kernel will not panic.

A panic might happen at the scheduler `proc.c:sched()`. A timer interrupt might
happen during `filealloc()` if interrupt is not disabled.

## xv6 lock implementation

The lock data must be modified only if the lock is being held.
