# Lab 5: File system, Spawn and Shell

## Questions

1. Do you have to do anything else to ensure that this I/O privilege setting is saved and restored properly when you subsequently switch from one environment to another? Why?

No. x86 interrupt handling will automatically save/restore eflags register.

## Challenge

### Interrupt-driven IDE disk access

```
Challenge! Implement interrupt-driven IDE disk access, with or without DMA. You can decide whether to move the device driver into the kernel, keep it in user space along with the file system, or even (if you really want to get into the micro-kernel spirit) move it into a separate environment of its own.
```

#### Design

In `fs/ide.c:ide_wait_ready()`, we have

```c
static int
ide_wait_ready(bool check_error)
{
	int r;

	while (((r = inb(0x1F7)) & (IDE_BSY|IDE_DRDY)) != IDE_DRDY)
		/* do nothing */;

	if (check_error && (r & (IDE_DF|IDE_ERR)) != 0)
		return -1;
	return 0;
}
```

Which polls until the IDE device is ready.
By section 6.3 in the [ATA Specification](https://pdos.csail.mit.edu/6.828/2018/readings/hardware/ATA-d1410r3a.pdf),
the IDE device will generate an interrupt when

> 1) any command except a PIO data-in command reaches command completion successfully;
> 3) the device is ready to send a data block during a PIO data-in command;
> 4) the device is ready to accept a data block after the first data block during a PIO data-out
command;

In `inc/trap.h`, we have a definition for IDE interrupt:
```
#define IRQ_IDE         14
```

We will modify `ide_wait_ready()`, so that the busy loop is replaced by a sleep
until the IDE interrupt has arriced.

#### Implementation

##### Wait trap system call

We define a new system call, `int sys_wait_trap(int trapno)`, which puts the program
into sleep until a certain interrupt is arrived.

We will implement this system call in a similar manner as `sys_ipc_*`.
First, we will need to flag an environment that is waiting for the
system call.

Our design potentially allows waiting for an arbitrary trap, thus
-1 is chosen instead of 0 (`T_DIVIDE`) to indicate that the process is not waiting.

```diff
diff --git a/inc/env.h b/inc/env.h
index ab392db..5ffd75a 100644
--- a/inc/env.h
+++ b/inc/env.h
@@ -66,6 +66,8 @@ struct Env {
 	uint32_t env_ipc_value;		// Data value sent to us
 	envid_t env_ipc_from;		// envid of the sender
 	int env_ipc_perm;		// Perm of page mapping received
+
+	int env_wait_trap;
 };

 #endif // !JOS_INC_ENV_H
diff --git a/kern/env.c b/kern/env.c
index 1d80f6b..1283a2f 100644
--- a/kern/env.c
+++ b/kern/env.c
@@ -288,6 +288,8 @@ _env_alloc(struct Env **newenv_store, envid_t parent_id)
 	// Also clear the IPC receiving flag.
 	e->env_ipc_recving = 0;

+	e->env_wait_trap = -1;
+
 	// commit the allocation
 	env_free_list = e->env_link;
 	*newenv_store = e;
```

Then we implement the system call in the kernel side: just set the flag to
the requested trap number, and then put the process into sleep.

```diff
diff --git a/inc/syscall.h b/inc/syscall.h
index 20c6433..28dac07 100644
--- a/inc/syscall.h
+++ b/inc/syscall.h
@@ -17,6 +17,7 @@ enum {
 	SYS_yield,
 	SYS_ipc_try_send,
 	SYS_ipc_recv,
+	SYS_wait_trap,
 	NSYSCALLS
 };

diff --git a/kern/syscall.c b/kern/syscall.c
index 8929a29..95dc0e1 100644
--- a/kern/syscall.c
+++ b/kern/syscall.c
@@ -456,6 +456,23 @@ sys_ipc_recv(void *dstva)
 	return 0;
 }

+// Block until an interrupt is arrived.
+static int
+sys_wait_trap(uint32_t trapno) {
+	switch (trapno) {
+		case IRQ_OFFSET + IRQ_IDE:
+			break;
+		default:
+			return -E_INVAL;
+	}
+	spin_lock(&env_lock);
+	assert(curenv->env_wait_trap == -1);
+	curenv->env_wait_trap = trapno;
+	curenv->env_status = ENV_NOT_RUNNABLE;
+	spin_unlock(&env_lock);
+	return 0;
+}
+
 // Dispatches to the correct kernel function, passing the arguments.
 int32_t
 syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
@@ -509,6 +526,9 @@ syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
 		case SYS_ipc_recv:
 			ret = sys_ipc_recv((void *) a1);
 			break;
+		case SYS_wait_trap:
+			ret = sys_wait_trap((uint32_t) a1);
+			break;
 		default:
 			ret = -E_INVAL;
 			break;
```

Then modify corresponding code for userspace library.

Note that the second argument for `syscall()` is `check` instead of `a1`.

```diff
diff --git a/inc/lib.h b/inc/lib.h
index 82f9373..174fa30 100644
--- a/inc/lib.h
+++ b/inc/lib.h
@@ -58,6 +58,7 @@ int	sys_page_map(envid_t src_env, void *src_pg,
 int	sys_page_unmap(envid_t env, void *pg);
 int	sys_ipc_try_send(envid_t to_env, uint32_t value, void *pg, int perm);
 int	sys_ipc_recv(void *rcv_pg);
+int	sys_wait_trap(uint32_t trapno);

 // This must be inlined.  Exercise for reader: why?
 static inline envid_t __attribute__((always_inline))
diff --git a/lib/syscall.c b/lib/syscall.c
index 4d4c3e8..308b8fe 100644
--- a/lib/syscall.c
+++ b/lib/syscall.c
@@ -19,6 +19,7 @@ syscall(int num, int check, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
 	// The last clause tells the assembler that this can
 	// potentially change the condition codes and arbitrary
 	// memory locations.
+	assert(check == 0 || check == 1);

 	asm volatile("int %1\n"
 		     : "=a" (ret)
@@ -117,3 +118,8 @@ sys_ipc_recv(void *dstva)
 	return syscall(SYS_ipc_recv, 1, (uint32_t)dstva, 0, 0, 0, 0);
 }

+int
+sys_wait_trap(uint32_t trapno)
+{
+	return syscall(SYS_wait_trap, 1, trapno, 0, 0, 0, 0);
+}
```

If a corresponding interrupt happens,
the kernel should wake up any processes that is waiting.

The `outb(IO_PIC2, 0x20)` instruction is very important. See the section
"Enabling the IDE IRQ" for details.

There is an order change for handling `IRQ_TIMER`, which doesn't matter.

```diff
diff --git a/kern/trap.c b/kern/trap.c
index 0febf74..862a391 100644
--- a/kern/trap.c
+++ b/kern/trap.c
@@ -222,10 +222,15 @@ trap_dispatch(struct Trapframe *tf)
 	// LAB 4: Your code here.
 	switch (tf->tf_trapno) {
 		case IRQ_OFFSET + IRQ_TIMER:
-			spin_lock(&env_lock);
 			lapic_eoi();
+			spin_lock(&env_lock);
 			sched_yield();
 			return;
+	}
+
+	// Handle keyboard and serial interrupts.
+	// LAB 5: Your code here.
+	switch (tf->tf_trapno) {
 		case IRQ_OFFSET + IRQ_KBD:
 			lapic_eoi();
 			kbd_intr();
@@ -234,11 +239,13 @@ trap_dispatch(struct Trapframe *tf)
 			lapic_eoi();
 			serial_intr();
 			return;
+		case IRQ_OFFSET + IRQ_IDE:
+			lapic_eoi();
+			outb(IO_PIC2, 0x20);  // Acknowledge EOI
+			wait_trap_handler(tf);
+			return;
 	}

-	// Handle keyboard and serial interrupts.
-	// LAB 5: Your code here.
-
 	// Unexpected trap: The user process or the kernel has a bug.
 	print_trapframe(tf);
 	if (tf->tf_cs == GD_KT)
@@ -398,3 +405,19 @@ page_fault_handler(struct Trapframe *tf)
 	env_destroy(curenv);
 }

+void
+wait_trap_handler(struct Trapframe *tf) {
+	size_t envx;
+	struct Env *e;
+
+	spin_lock(&env_lock);
+	for (envx = 0; envx < NENV; envx++) {
+		e = &envs[envx];
+		if (e->env_status == ENV_NOT_RUNNABLE && e->env_wait_trap == tf->tf_trapno) {
+			e->env_wait_trap = -1;
+			e->env_tf.tf_regs.reg_eax = 0;
+			e->env_status = ENV_RUNNABLE;
+		}
+	}
+	spin_unlock(&env_lock);
+}
diff --git a/kern/trap.h b/kern/trap.h
index 36b8758..53612e9 100644
--- a/kern/trap.h
+++ b/kern/trap.h
@@ -18,6 +18,7 @@ void trap_init_percpu(void);
 void print_regs(struct PushRegs *regs);
 void print_trapframe(struct Trapframe *tf);
 void page_fault_handler(struct Trapframe *);
+void wait_trap_handler(struct Trapframe *);
 void backtrace(struct Trapframe *);

 #endif /* JOS_KERN_TRAP_H */
```

##### Enabling the IDE IRQ

We will also need to turn on the `IRQ_OFFSET + IRQ_IDE` interrupt using
the 8259A device,
in a similar manner as how keyboard and serial input is set up:
```diff
diff --git a/kern/init.c b/kern/init.c
index f9f3a76..144414c 100644
--- a/kern/init.c
+++ b/kern/init.c
@@ -33,6 +33,7 @@ i386_init(void)
 	// Lab 3 user environment initialization functions
 	env_init();
 	trap_init();
+	irq_setmask_8259A(irq_mask_8259A & ~(1<<IRQ_IDE));
 
 	// Lab 4 multiprocessor initialization functions
 	mp_init();
```

However, without the statement `outb(IO_PIC2, 0x20)` in `kern/trap.c:trap_dispatch()`,
only one `IRQ_IDE` is received after the first `ide_read()` call.

Since we are using the i8259A controller, I digged into QEMU's emulation code
and enabled debugging at [hw/intc/i8259.c](https://github.com/mit-pdos/6.828-qemu/blob/d531b1b1d6b7696dfd9695c1d560e3df53e615c5/hw/intc/i8259.c#L32)

```c
#define DEBUG_PIC
```

From the output, I found that the interrupt is correctly generated by the IDE drive,
as there are [debug statements](https://github.com/mit-pdos/6.828-qemu/blob/d531b1b1d6b7696dfd9695c1d560e3df53e615c5/hw/intc/i8259.c#L142)
from `pic_set_irq()` showing that IRQ 14 is being set:


```c
/* set irq level. If an edge is detected, then the IRR is set to 1 */
static void pic_set_irq(void *opaque, int irq, int level)
{
...
#if defined(DEBUG_PIC) || defined(DEBUG_IRQ_COUNT)
    if (level != irq_level[irq_index]) {
        DPRINTF("pic_set_irq: irq=%d level=%d\n", irq_index, level);
        irq_level[irq_index] = level;
...
    pic_update_irq(s);
}
```

The function `pic_set_irq()` in turn calls `pic_update_irq()`,
but only one [debug statement](https://github.com/mit-pdos/6.828-qemu/blob/d531b1b1d6b7696dfd9695c1d560e3df53e615c5/hw/intc/i8259.c#L116)
from `pic_update_irq()` that actually raises the IRQ,
which corresponds to the only `IRQ_IDE` received by the kernel.

```c
/* Update INT output. Must be called every time the output may have changed. */
static void pic_update_irq(PICCommonState *s)
{
    int irq;

    irq = pic_get_irq(s);
    if (irq >= 0) {
        DPRINTF("pic%d: imr=%x irr=%x padd=%d\n",
                s->master ? 0 : 1, s->imr, s->irr, s->priority_add);
        qemu_irq_raise(s->int_out[0]);
    } else {
        qemu_irq_lower(s->int_out[0]);
    }
}
```

This implies that the IRQ is emitted by the IDE, but is somehow masked by the
i8259A controller. Perhaps we are missing some EOI statement, as we only sent
the EOI to the local APIC device.

On [this website](http://www.brokenthorn.com/Resources/OSDevPic.html) I found
that there are some assembly statement for sending EOI to i8259A:
```asm
; send EOI to primary PIC

	mov	al, 0x20	; set bit 4 of OCW 2
	out	0x20, al	; write to primary PIC command register
```

Since `IRQ_IDE` (14) corresponds to the slave controller, I tried to add this statement
```c
outb(IO_PIC2, 0x20);  // Acknowledge EOI
```

And the interrupt is generated correctly.

But there is no such special handling for `IRQ_KBD` or `IRQ_SERIAL`.
After inspecting `kern/picirq.c` again,
I found that there is "Automatic EOI" for `IO_PIC1`:
```c
	// ICW4:  000nbmap
	//    n:  1 = special fully nested mode
	//    b:  1 = buffered mode
	//    m:  0 = slave PIC, 1 = master PIC
	//	  (ignored when b is 0, as the master/slave role
	//	  can be hardwired).
	//    a:  1 = Automatic EOI mode
	//    p:  0 = MCS-80/85 mode, 1 = intel x86 mode
	outb(IO_PIC1+1, 0x3);
```

However, for `IO_PIC2`, the automatic EOI is disabled.
```c
	// NB Automatic EOI mode doesn't tend to work on the slave.
	// Linux source code says it's "to be investigated".
	outb(IO_PIC2+1, 0x01);			// ICW4
```

This explains that why `IRQ_KBD` and `IRQ_SERIAL` works fine: they are controlled
by the master controller, which has automatic EOI enabled.

##### Interrupt-driven IDE driver

Finally, we can modify `fs/ide.c:ide_wait_ready()` to sleep when the device
is not ready:

```diff
diff --git a/fs/ide.c b/fs/ide.c
index 2d8b4bf..838b0d2 100644
--- a/fs/ide.c
+++ b/fs/ide.c
@@ -19,8 +19,10 @@ ide_wait_ready(bool check_error)
 {
 	int r;
 
-	while (((r = inb(0x1F7)) & (IDE_BSY|IDE_DRDY)) != IDE_DRDY)
-		/* do nothing */;
+	while (((r = inb(0x1F7)) & (IDE_BSY|IDE_DRDY)) != IDE_DRDY) {
+		// sys_yield();
+		assert(sys_wait_trap(IRQ_OFFSET + IRQ_IDE) == 0);
+	}
 
 	if (check_error && (r & (IDE_DF|IDE_ERR)) != 0)
 		return -1;
@@ -69,6 +71,7 @@ ide_read(uint32_t secno, void *dst, size_t nsecs)
 
 	ide_wait_ready(0);
 
+	outb(0x3F6, 0);  // generate IRQ
 	outb(0x1F2, nsecs);
 	outb(0x1F3, secno & 0xFF);
 	outb(0x1F4, (secno >> 8) & 0xFF);
@@ -94,6 +97,7 @@ ide_write(uint32_t secno, const void *src, size_t nsecs)
 
 	ide_wait_ready(0);
 
+	outb(0x3F6, 0);  // generate IRQ
 	outb(0x1F2, nsecs);
 	outb(0x1F3, secno & 0xFF);
 	outb(0x1F4, (secno >> 8) & 0xFF);
```

Note that without `outb(0x3F6, 0)` the driver still works fine.
But I followed xv6's IDE driver to clear the `nIEN` bit.

##### Sched fix

There might be some situation that every process is sleeping by `sys_wait_trap`,
thus I made `sched_yield()` to also count `ENV_NOT_RUNNABLE` processes:

```diff
diff --git a/kern/sched.c b/kern/sched.c
index fb0fdeb..9a5d4c9 100644
--- a/kern/sched.c
+++ b/kern/sched.c
@@ -68,8 +68,7 @@ sched_halt(void)
 	for (i = 0; i < NENV; i++) {
 		if ((envs[i].env_status == ENV_RUNNABLE ||
 		     envs[i].env_status == ENV_RUNNING ||
-		     envs[i].env_status == ENV_DYING ||
-		     envs[i].env_status == ENV_NOT_RUNNABLE))
+		     envs[i].env_status == ENV_DYING))
 			break;
 	}
 	// Note: at this point we don't need to hold env_lock, but unlocking
```

#### Validation

To validate the modifications, we added the following code:

```diff
diff --git a/fs/ide.c b/fs/ide.c
index 838b0d2..e23130e 100644
--- a/fs/ide.c
+++ b/fs/ide.c
@@ -14,15 +14,23 @@

 static int diskno = 1;

+static unsigned wait_syscall = 0, wait_call = 0;
+
 static int
 ide_wait_ready(bool check_error)
 {
 	int r;

+	unsigned count = 0;
+	wait_call++;
 	while (((r = inb(0x1F7)) & (IDE_BSY|IDE_DRDY)) != IDE_DRDY) {
+		count++;
 		// sys_yield();
 		assert(sys_wait_trap(IRQ_OFFSET + IRQ_IDE) == 0);
 	}
+	wait_syscall += count;
+
+	cprintf("ide_wait_ready: (+%u) %u/%u=%lf\n", count, wait_syscall, wait_call, (double) wait_syscall / wait_call);

 	if (check_error && (r & (IDE_DF|IDE_ERR)) != 0)
 		return -1;
```

We track the following variables:

| Symbol | Meaning |
|-|-|
| `wait_call` | Total number of calls to `ide_wait_ready()` |
| `wait_syscall` | Total number of calls to `sys_wait_trap()` from `ide_wait_ready()` |
| `count` | Number of calls to `sys_wait_trap()` within a single invocation. |

If our implementation is effective, then `count` or average `wait_syscall / wait_call`
would be low and constant.

The following is the last few lines if we are running `qemu-nox`:

```
ide_wait_ready: (+0) 248/290=0.855172
ide_wait_ready: (+1) 249/291=0.855670
ide_wait_ready: (+1) 250/292=0.856164
ide_wait_ready: (+1) 251/293=0.856655
ide_wait_ready: (+1) 252/294=0.857142
ide_wait_ready: (+1) 253/295=0.857627
ide_wait_ready: (+1) 254/296=0.858108
ide_wait_ready: (+1) 255/297=0.858585
ide_wait_ready: (+1) 256/298=0.859060
$ QEMU: Terminated
```

The `count` is either 0 or 1, and the average system calls per wait is roughly 0.85.

We also conducted an experiment to test whether this system call is more effective
than simply yielding to kernel:

There are two independent variables: the sleep method and disk performance.

- Sleep method: `sys_wait_trap()` or `sys_yield()` (control)
- Disk performance: native (no throttling) or `iops-total=100`

Disk throttling can be achieved by the following code:
```diff
diff --git a/GNUmakefile b/GNUmakefile
index fb9261a..f14bb8e 100644
--- a/GNUmakefile
+++ b/GNUmakefile
@@ -150,7 +150,7 @@ QEMUOPTS = -drive file=$(OBJDIR)/kern/kernel.img,index=0,media=disk,format=raw -
 QEMUOPTS += $(shell if $(QEMU) -nographic -help | grep -q '^-D '; then echo '-D qemu.log'; fi)
 IMAGES = $(OBJDIR)/kern/kernel.img
 QEMUOPTS += -smp $(CPUS)
-QEMUOPTS += -drive file=$(OBJDIR)/fs/fs.img,index=1,media=disk,format=raw
+QEMUOPTS += -drive file=$(OBJDIR)/fs/fs.img,index=1,media=disk,format=raw,throttling.iops-total=100
 IMAGES += $(OBJDIR)/fs/fs.img
 QEMUOPTS += $(QEMUEXTRA)
```

Result (taking the last output):

| | `sys_wait_trap()` | `sys_yield()` |
|-|-|-|
| native | `ide_wait_ready: (+1) 254/298=0.852348` | `ide_wait_ready: (+3) 388/298=1.302013` |
| `iops-total=100` | `ide_wait_ready: (+1) 264/298=0.885906` | `ide_wait_ready: (+423) 85113/298=285.614093` |

The difference is not significant with native speed,
but there is a large difference with throttled disks.

#### Limitations

- The syscall `sys_wait_trap()` only supports the IDE interrupt.
- The wake up mechanism in `wait_trap_handler()` is inefficient as it walks through
every process. But works currently as we only have `1<<10 = 1024` processes.
- Design choice: call `sched_yield()` if any process should be waken up (rather than resuming current process).
- DMA is not used. Using DMA would improve the performance, but might make
the system more complex.
