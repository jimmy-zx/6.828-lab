# Questions

1. `mpentry.S` is linked at high address but loaded at low address. The macro is required to manually convert high address to low address.

2. During interrupt handling, the cpu will push some data before calling the interrupt handler, which might corrupt the stack.

3. The VA sapce of all envs is identical above UTOP (except UVPT), which includes the kernel address space.

4. The environment switch should be transparent to the user so that the user does not need extra handling. The registers are saved by the trap handler and restored by `env_pop_tf()`.

# Challenge

## Find-grained locking

```
Challenge! The big kernel lock is simple and easy to use. Nevertheless, it eliminates all concurrency in kernel mode. Most modern operating systems use different locks to protect different parts of their shared state, an approach called fine-grained locking. Fine-grained locking can increase performance significantly, but is more difficult to implement and error-prone. If you are brave enough, drop the big kernel lock and embrace concurrency in JOS!

It is up to you to decide the locking granularity (the amount of data that a lock protects). As a hint, you may consider using spin locks to ensure exclusive access to these shared components in the JOS kernel:

    The page allocator.
    The console driver.
    The scheduler.
    The inter-process communication (IPC) state that you will implement in the part C.
```

### Basic Analysis

The following resources are shared and need to be protected:

- `kern/pmap.c`: `pages`, `page_free_list`, `user_mem_check_addr`.
- `kern/env.c`: `env_status` field of `envs`, `env_free_list`.
- `kern/console.c`: the console device.

We defined three locks in `kern/spinlock.c`, in order:

- `env_lock`
- `pmap_lock`
- `console_lock`

Some useful tricks during implementation:

1. Ensure programs that only uses currently implemented features work right.
2. For lockless functions: check if the lock is held.
3. Kernel monitor: add a custom panic call with trapframe.

### console

For the console device, we need to guarantee the input is captured
by only one CPU and every CPU is able to output a whole sequence. Therefore,
we add a lock for `cons_getc()` and `vcprintf()`.

```c
diff --git a/kern/console.c b/kern/console.c
index 329ca50..ab96537 100644
--- a/kern/console.c
+++ b/kern/console.c
@@ -9,6 +9,7 @@
 #include <kern/console.h>
 #include <kern/trap.h>
 #include <kern/picirq.h>
+#include <kern/spinlock.h>

 static void cons_intr(int (*proc)(void));
 static void cons_putc(int c);
@@ -422,6 +423,7 @@ cons_getc(void)
 	// poll for any pending input characters,
 	// so that this function works even when interrupts are disabled
 	// (e.g., when called from the kernel monitor).
+	spin_lock(&cons_lock);
 	serial_intr();
 	kbd_intr();

@@ -430,8 +432,10 @@ cons_getc(void)
 		c = cons.buf[cons.rpos++];
 		if (cons.rpos == CONSBUFSIZE)
 			cons.rpos = 0;
+		spin_unlock(&cons_lock);
 		return c;
 	}
+	spin_unlock(&cons_lock);
 	return 0;
 }

diff --git a/kern/printf.c b/kern/printf.c
index 6932ca5..0be4bad 100644
--- a/kern/printf.c
+++ b/kern/printf.c
@@ -5,6 +5,8 @@
 #include <inc/stdio.h>
 #include <inc/stdarg.h>

+#include <kern/spinlock.h>
+

 static void
 putch(int ch, int *cnt)
@@ -18,7 +20,9 @@ vcprintf(const char *fmt, va_list ap)
 {
 	int cnt = 0;

+	spin_lock(&cons_lock);
 	vprintfmt((void*)putch, &cnt, fmt, ap);
+	spin_unlock(&cons_lock);
 	return cnt;
 }
```

### pmap

For the resources provided by `kern/pmap.c`, the resources are only used
internally. Therefore, we added a lock for every (public) function that
accesses `pages` or `page_free_list`. The lock will be transparent to the caller.

To avoid internal deadlocking, we provided a lockless version of the modified functions
starting with `_`. The lockless version will check if the lock is held. [^1]

```c
diff --git a/kern/pmap.c b/kern/pmap.c
index 31b9d53..7a71ea4 100644
--- a/kern/pmap.c
+++ b/kern/pmap.c
@@ -11,6 +11,7 @@
 #include <kern/kclock.h>
 #include <kern/env.h>
 #include <kern/cpu.h>
+#include <kern/spinlock.h>

 // These variables are set by i386_detect_memory()
 size_t npages;			// Amount of physical memory (in pages)
@@ -21,6 +22,13 @@ pde_t *kern_pgdir;		// Kernel's initial page directory
 struct PageInfo *pages;		// Physical page state array
 static struct PageInfo *page_free_list;	// Free list of physical pages

+static void _page_remove(pde_t *pgdir, void *va);
+static pte_t *_pgdir_walk(pde_t *pgdir, const void *va, int create);
+static struct PageInfo *_page_alloc(int alloc_flags);
+static struct PageInfo *_page_lookup(pde_t *pgdir, void *va, pte_t **pte_store);
+static void _page_decref(struct PageInfo* pp);
+static void _page_free(struct PageInfo *pp);
+static void _boot_map_region(pde_t *pgdir, uintptr_t va, size_t size, physaddr_t pa, int perm);


 // --------------------------------------------------------------
@@ -376,11 +384,12 @@ page_init(void)
 // Returns NULL if out of free memory.
 //
 // Hint: use page2kva and memset
-struct PageInfo *
-page_alloc(int alloc_flags)
+static struct PageInfo *
+_page_alloc(int alloc_flags)
 {
 	struct PageInfo *newpage;

+	assert(pmap_lock.locked);
 	if ((newpage = page_free_list) == NULL) {
 		return NULL;
 	}
@@ -392,33 +401,62 @@ page_alloc(int alloc_flags)
 	return newpage;
 }

+struct PageInfo *
+page_alloc(int alloc_flags)
+{
+	struct PageInfo *pp;
+	spin_lock(&pmap_lock);
+	pp = _page_alloc(alloc_flags);
+	spin_unlock(&pmap_lock);
+	return pp;
+}
+
 //
 // Return a page to the free list.
 // (This function should only be called when pp->pp_ref reaches 0.)
 //
-void
-page_free(struct PageInfo *pp)
+static void
+_page_free(struct PageInfo *pp)
 {
+	assert(pmap_lock.locked);
 	assert(pp->pp_ref == 0);
 	if (pp < pages || pp > pages + npages) {
+		spin_unlock(&pmap_lock);
 		panic("page_free: out of range %p\n", pp);
 	}
 	if (pp->pp_link != NULL) {
+		spin_unlock(&pmap_lock);
 		panic("page_free: double free on %p\n", pp);
 	}
 	pp->pp_link = page_free_list;
 	page_free_list = pp;
 }

+void
+page_free(struct PageInfo *pp) {
+	spin_lock(&pmap_lock);
+	_page_free(pp);
+	spin_unlock(&pmap_lock);
+}
+
 //
 // Decrement the reference count on a page,
 // freeing it if there are no more refs.
 //
+static void
+_page_decref(struct PageInfo* pp)
+{
+	assert(pmap_lock.locked);
+	if (--pp->pp_ref == 0)
+		_page_free(pp);
+}
+
 void
 page_decref(struct PageInfo* pp)
 {
-	if (--pp->pp_ref == 0)
-		page_free(pp);
+	spin_lock(&pmap_lock);
+	_page_decref(pp);
+	spin_unlock(&pmap_lock);
 }

 // Given 'pgdir', a pointer to a page directory, pgdir_walk returns
@@ -443,13 +481,14 @@ page_decref(struct PageInfo* pp)
 // Hint 3: look at inc/mmu.h for useful macros that manipulate page
 // table and page directory entries.
 //
-pte_t *
-pgdir_walk(pde_t *pgdir, const void *va, int create)
+static pte_t *
+_pgdir_walk(pde_t *pgdir, const void *va, int create)
 {
+	assert(pmap_lock.locked);
 	struct PageInfo *pp = NULL;
 	pde_t *const pde = &pgdir[PDX(va)];
 	if (!(*pde & PTE_P)) {
-		if (!create || ((pp = page_alloc(ALLOC_ZERO)) == NULL)) {
+		if (!create || ((pp = _page_alloc(ALLOC_ZERO)) == NULL)) {
 			return NULL;
 		}
 		pp->pp_ref++;
@@ -458,6 +497,16 @@ pgdir_walk(pde_t *pgdir, const void *va, int create)
 	return (pte_t *)KADDR(PTE_ADDR(*pde)) + PTX(va);
 }

+pde_t *
+pgdir_walk(pde_t *pgdir, const void *va, int create) {
+	pde_t *res;
+
+	spin_lock(&pmap_lock);
+	res = _pgdir_walk(pgdir, va, create);
+	spin_unlock(&pmap_lock);
+	return res;
+}
+
 //
 // Map [va, va+size) of virtual address space to physical [pa, pa+size)
 // in the page table rooted at pgdir.  Size is a multiple of PGSIZE, and
@@ -470,18 +519,27 @@ pgdir_walk(pde_t *pgdir, const void *va, int create)
 //
 // Hint: the TA solution uses pgdir_walk
 static void
-boot_map_region(pde_t *pgdir, uintptr_t va, size_t size, physaddr_t pa, int perm)
+_boot_map_region(pde_t *pgdir, uintptr_t va, size_t size, physaddr_t pa, int perm)
 {
 	size_t i;
 	pte_t *pte;
+	assert(pmap_lock.locked);
 	for (i = 0; i < size; i += PGSIZE) {
-		if ((pte = pgdir_walk(kern_pgdir, (const void *)(va + i), 1)) == NULL) {
+		if ((pte = _pgdir_walk(kern_pgdir, (const void *)(va + i), 1)) == NULL) {
 			panic("boot_map_region: pgdir_walk fails\n");
 		}
 		*pte = (pa + i) | perm | PTE_P;
 	}
 }

+static void
+boot_map_region(pde_t *pgdir, uintptr_t va, size_t size, physaddr_t pa, int perm)
+{
+	spin_lock(&pmap_lock);
+	_boot_map_region(pgdir, va, size, pa, perm);
+	spin_unlock(&pmap_lock);
+}
+
 //
 // Map the physical page 'pp' at virtual address 'va'.
 // The permissions (the low 12 bits) of the page table entry
@@ -511,14 +569,17 @@ int
 page_insert(pde_t *pgdir, struct PageInfo *pp, void *va, int perm)
 {
 	pte_t *pte;
-	if ((pte = pgdir_walk(pgdir, va, 1)) == NULL) {
+	spin_lock(&pmap_lock);
+	if ((pte = _pgdir_walk(pgdir, va, 1)) == NULL) {
+		spin_unlock(&pmap_lock);
 		return -E_NO_MEM;
 	}
 	pp->pp_ref++;  // increment first so that page_remove will not free when inserting the same page
 	if (*pte & PTE_P) {
-		page_remove(pgdir, va);
+		_page_remove(pgdir, va);
 	}
 	*pte = page2pa(pp) | perm | PTE_P;
+	spin_unlock(&pmap_lock);
 	return 0;
 }

@@ -533,11 +594,12 @@ page_insert(pde_t *pgdir, struct PageInfo *pp, void *va, int perm)
 //
 // Hint: the TA solution uses pgdir_walk and pa2page.
 //
-struct PageInfo *
-page_lookup(pde_t *pgdir, void *va, pte_t **pte_store)
+static struct PageInfo *
+_page_lookup(pde_t *pgdir, void *va, pte_t **pte_store)
 {
 	pte_t *pte;
-	if ((pte = pgdir_walk(pgdir, va, 0)) == NULL || !(*pte & PTE_P)) {
+	assert(pmap_lock.locked);
+	if ((pte = _pgdir_walk(pgdir, va, 0)) == NULL || !(*pte & PTE_P)) {
 		return NULL;
 	}
 	if (pte_store != NULL) {
@@ -546,6 +608,17 @@ page_lookup(pde_t *pgdir, void *va, pte_t **pte_store)
 	return pa2page(PTE_ADDR(*pte));
 }

+struct PageInfo *
+page_lookup(pde_t *pgdir, void *va, pte_t **pte_store)
+{
+	struct PageInfo *pp;
+	spin_lock(&pmap_lock);
+	pp = _page_lookup(pgdir, va, pte_store);pte_store;
+	spin_unlock(&pmap_lock);
+	return pp;
+}
+
+
 //
 // Unmaps the physical page at virtual address 'va'.
 // If there is no physical page at that address, silently does nothing.
@@ -561,19 +634,27 @@ page_lookup(pde_t *pgdir, void *va, pte_t **pte_store)
 // Hint: The TA solution is implemented using page_lookup,
 // 	tlb_invalidate, and page_decref.
 //
-void
-page_remove(pde_t *pgdir, void *va)
+static void
+_page_remove(pde_t *pgdir, void *va)
 {
 	pte_t *pte;
 	struct PageInfo *pp;
-	if ((pp = page_lookup(pgdir, va, &pte)) == NULL) {
+	assert(pmap_lock.locked);
+	if ((pp = _page_lookup(pgdir, va, &pte)) == NULL) {
 		return;
 	}
 	*pte = 0;
-	page_decref(pp);
+	_page_decref(pp);
 	tlb_invalidate(pgdir, va);
 }

+void
+page_remove(pde_t *pgdir, void *va) {
+	spin_lock(&pmap_lock);
+	_page_remove(pgdir, va);
+	spin_unlock(&pmap_lock);
+}
+
 //
 // Invalidate a TLB entry, but only if the page tables being
 // edited are the ones currently in use by the processor.
@@ -618,18 +699,21 @@ mmio_map_region(physaddr_t pa, size_t size)
 	// Hint: The staff solution uses boot_map_region.
 	//
 	// Your code here:
+	spin_lock(&pmap_lock);
 	physaddr_t pa_start = ROUNDDOWN(pa, PGSIZE);
 	physaddr_t pa_end = ROUNDUP(pa + size, PGSIZE);
 	size_t actual_size = pa_end - pa_start;
 	void *const old_base = (void *)base;

 	if (base + actual_size > MMIOLIM) {
+		spin_unlock(&pmap_lock);
 		panic("mmio_map_region: pa_end > MMIOLIM\n");
 	}

-	boot_map_region(kern_pgdir, base, actual_size, pa_start, PTE_PCD | PTE_PWT | PTE_W);
+	_boot_map_region(kern_pgdir, base, actual_size, pa_start, PTE_PCD | PTE_PWT | PTE_W);
 	base += actual_size;

+	spin_unlock(&pmap_lock);
 	return old_base;
 }

@@ -656,16 +740,19 @@ static uintptr_t user_mem_check_addr;
 int
 user_mem_check(struct Env *env, const void *va, size_t len, int perm)
 {
+	spin_lock(&pmap_lock);
 	uintptr_t cur = (uintptr_t)ROUNDDOWN(va, PGSIZE);
 	uintptr_t end = (uintptr_t)ROUNDUP(va + len, PGSIZE);
 	pte_t *pte;
 	for (; cur < end; cur += PGSIZE) {
-		pte = pgdir_walk(env->env_pgdir, (void *)cur, 0);
+		pte = _pgdir_walk(env->env_pgdir, (void *)cur, 0);
 		if (cur >= ULIM || pte == NULL || !(*pte & PTE_P) || (*pte & perm) != perm) {
 			user_mem_check_addr = (cur < (uintptr_t) va) ? (uintptr_t) va : cur;
+			spin_unlock(&pmap_lock);
 			return -E_FAULT;
 		}
 	}
+	spin_unlock(&pmap_lock);
 	return 0;
 }
```

### env

For the resources provided by `kern/env.c`, we took the similar approach as `kern/pmap.c`:
provide a locked original function, and a lockless function starting with `_`.
However, `envs` are also used externally, and we need to cosider transfer of control.

Special cases for several functions:
`sched_yield()`, `sched_halt()`, `env_pop_tf()`, and `env_run()`.
The caller of these functions usually holds the lock and these function (usually) does
not return. Therefore these functions remains lockless, and a check is provided.

Modifications outside `kern/env.c`:

- `kern/env.h`: the lockless versions `_env_*` is exported.
- `kern/init.c`: the kernel holds `env_lock` before `boot_aps(0)` so that APs
will wait until the BSP is ready.
- `kern/sched.c`: `sched_yield()` and `sched_halt()` requires holding the lock.
`sched_yield()` releases the lock if the processor is going to sleep.
- `kern/syscall.c`: Locks will be hold by corresponding syscalls if `envs` is
going to be accessed.
- `kern/trap.c`: Locks will be hold before calling `env_run()` and `sched_yield()`.

```c
diff --git a/kern/env.c b/kern/env.c
index 93d184e..8062c5a 100644
--- a/kern/env.c
+++ b/kern/env.c
@@ -62,6 +62,12 @@ struct Pseudodesc gdt_pd = {
 	sizeof(gdt) - 1, (unsigned long) gdt
 };

+int _env_alloc(struct Env **newenv_store, envid_t parent_id);
+int _envid2env(envid_t envid, struct Env **env_store, bool checkperm);
+void _env_create(uint8_t *binary, enum EnvType type);
+void _env_free(struct Env *e);
+void _env_destroy(struct Env *e);
+
 //
 // Converts an envid to an env pointer.
 // If checkperm is set, the specified environment must be either the
@@ -73,10 +79,11 @@ struct Pseudodesc gdt_pd = {
 //   On error, sets *env_store to NULL.
 //
 int
-envid2env(envid_t envid, struct Env **env_store, bool checkperm)
+_envid2env(envid_t envid, struct Env **env_store, bool checkperm)
 {
 	struct Env *e;

+	assert(env_lock.locked);
 	// If envid is zero, return the current environment.
 	if (envid == 0) {
 		*env_store = curenv;
@@ -108,6 +115,16 @@ envid2env(envid_t envid, struct Env **env_store, bool checkperm)
 	return 0;
 }

+int
+envid2env(envid_t envid, struct Env **env_store, bool checkperm) {
+	int res;
+	spin_lock(&env_lock);
+	res = _envid2env(envid, env_store, checkperm);
+	spin_unlock(&env_lock);
+	return res;
+}
+
+
 // Mark all environments in 'envs' as free, set their env_ids to 0,
 // and insert them into the env_free_list.
 // Make sure the environments are in the free list in the same order
@@ -121,6 +138,7 @@ env_init(void)
 	// LAB 3: Your code here.
 	size_t i;

+	spin_lock(&env_lock);
 	env_free_list = NULL;
 	for (i = NENV - 1; i < NENV; i--) {
 		envs[i].env_status = ENV_FREE;
@@ -128,6 +146,7 @@ env_init(void)
 		envs[i].env_link = env_free_list;
 		env_free_list = &envs[i];
 	}
+	spin_unlock(&env_lock);

 	// Per-CPU part of the initialization
 	env_init_percpu();
@@ -170,6 +189,7 @@ env_setup_vm(struct Env *e)
 	int i;
 	struct PageInfo *p = NULL;

+	assert(env_lock.locked);
 	// Allocate a page for the page directory
 	if (!(p = page_alloc(ALLOC_ZERO)))
 		return -E_NO_MEM;
@@ -211,12 +231,13 @@ env_setup_vm(struct Env *e)
 //	-E_NO_MEM on memory exhaustion
 //
 int
-env_alloc(struct Env **newenv_store, envid_t parent_id)
+_env_alloc(struct Env **newenv_store, envid_t parent_id)
 {
 	int32_t generation;
 	int r;
 	struct Env *e;

+	assert(env_lock.locked);
 	if (!(e = env_free_list))
 		return -E_NO_FREE_ENV;

@@ -275,6 +296,15 @@ env_alloc(struct Env **newenv_store, envid_t parent_id)
 	return 0;
 }

+int
+env_alloc(struct Env **newenv_store, envid_t parent_id) {
+	int res;
+	spin_lock(&env_lock);
+	res = _env_alloc(newenv_store, parent_id);
+	spin_unlock(&env_lock);
+	return res;
+}
+
 //
 // Allocate len bytes of physical memory for environment env,
 // and map it at virtual address va in the environment's address space.
@@ -362,6 +392,7 @@ load_icode(struct Env *e, uint8_t *binary)
 	struct Proghdr *ph, *eph;
 	struct Elf *elf_header = (struct Elf *)binary;

+	assert(env_lock.locked);
 	if (elf_header->e_magic != ELF_MAGIC) {
 		panic("load_icode: invalid elf hader found\n");
 	}
@@ -400,13 +431,14 @@ load_icode(struct Env *e, uint8_t *binary)
 // The new env's parent ID is set to 0.
 //
 void
-env_create(uint8_t *binary, enum EnvType type)
+_env_create(uint8_t *binary, enum EnvType type)
 {
 	// LAB 3: Your code here.
 	struct Env *env;
 	int r;

-	if ((r = env_alloc(&env, 0)) < 0) {
+	assert(env_lock.locked);
+	if ((r = _env_alloc(&env, 0)) < 0) {
 		panic("env_create: env_alloc fails %e\n", r);
 	}

@@ -415,16 +447,24 @@ env_create(uint8_t *binary, enum EnvType type)
 	load_icode(env, binary);
 }

+void
+env_create(uint8_t *binary, enum EnvType type) {
+	spin_lock(&env_lock);
+	_env_create(binary, type);
+	spin_unlock(&env_lock);
+}
+
 //
 // Frees env e and all memory it uses.
 //
 void
-env_free(struct Env *e)
+_env_free(struct Env *e)
 {
 	pte_t *pt;
 	uint32_t pdeno, pteno;
 	physaddr_t pa;

+	assert(env_lock.locked);
 	// If freeing the current environment, switch to kern_pgdir
 	// before freeing the page directory, just in case the page
 	// gets reused.
@@ -468,14 +508,22 @@ env_free(struct Env *e)
 	env_free_list = e;
 }

+void
+env_free(struct Env *e) {
+	spin_lock(&env_lock);
+	_env_free(e);
+	spin_unlock(&env_lock);
+}
+
 //
 // Frees environment e.
 // If e was the current env, then runs a new environment (and does not return
 // to the caller).
 //
 void
-env_destroy(struct Env *e)
+_env_destroy(struct Env *e)
 {
+	assert(env_lock.locked);
 	// If e is currently running on other CPUs, we change its state to
 	// ENV_DYING. A zombie environment will be freed the next time
 	// it traps to the kernel.
@@ -484,7 +532,7 @@ env_destroy(struct Env *e)
 		return;
 	}

-	env_free(e);
+	_env_free(e);

 	if (curenv == e) {
 		curenv = NULL;
@@ -493,6 +541,14 @@ env_destroy(struct Env *e)
 }


+void
+env_destroy(struct Env *e) {
+	spin_lock(&env_lock);
+	_env_destroy(e);
+	spin_unlock(&env_lock);
+}
+
+
 //
 // Restores the register values in the Trapframe with the 'iret' instruction.
 // This exits the kernel and starts executing some environment's code.
@@ -502,8 +558,10 @@ env_destroy(struct Env *e)
 void
 env_pop_tf(struct Trapframe *tf)
 {
+	assert(env_lock.locked);
 	// Record the CPU we are running on for user-space debugging
 	curenv->env_cpunum = cpunum();
+	spin_unlock(&env_lock);

 	asm volatile(
 		"\tmovl %0,%%esp\n"
@@ -541,6 +599,7 @@ env_run(struct Env *e)
 	//	e->env_tf.  Go back through the code you wrote above
 	//	and make sure you have set the relevant parts of
 	//	e->env_tf to sensible values.
+	assert(env_lock.locked);
 	if (curenv != NULL && curenv->env_status == ENV_RUNNING) {
 		curenv->env_status = ENV_RUNNABLE;
 	}
diff --git a/kern/env.h b/kern/env.h
index 286ece7..ce5720e 100644
--- a/kern/env.h
+++ b/kern/env.h
@@ -33,4 +33,19 @@ void	env_pop_tf(struct Trapframe *tf) __attribute__((noreturn));
 			   type);					\
 	} while (0)

+// Must be holding env_lock
+#define _ENV_CREATE(x, type)						\
+	do {								\
+		extern uint8_t ENV_PASTE3(_binary_obj_, x, _start)[];	\
+		_env_create(ENV_PASTE3(_binary_obj_, x, _start),		\
+			   type);					\
+	} while (0)
+
+// The following functions requires holding env_lock
+int _env_alloc(struct Env **newenv_store, envid_t parent_id);
+int _envid2env(envid_t envid, struct Env **env_store, bool checkperm);
+void _env_create(uint8_t *binary, enum EnvType type);
+void _env_free(struct Env *e);
+void _env_destroy(struct Env *e);
+
 #endif // !JOS_KERN_ENV_H
diff --git a/kern/init.c b/kern/init.c
index e4f2435..45b3dfd 100644
--- a/kern/init.c
+++ b/kern/init.c
@@ -44,16 +44,17 @@ i386_init(void)
 	// Acquire the big kernel lock before waking up APs
 	// Your code here:
 	// lock_kernel();
+	spin_lock(&env_lock);

 	// Starting non-boot CPUs
 	boot_aps();

 #if defined(TEST)
 	// Don't touch -- used by grading script!
-	ENV_CREATE(TEST, ENV_TYPE_USER);
+	_ENV_CREATE(TEST, ENV_TYPE_USER);
 #else
 	// Touch all you want.
-	ENV_CREATE(user_dumbfork, ENV_TYPE_USER);
+	_ENV_CREATE(user_dumbfork, ENV_TYPE_USER);
 #endif // TEST*

 	// Schedule and run the first user environment!
@@ -111,6 +112,7 @@ mp_main(void)
 	//
 	// Your code here:
 	// lock_kernel();
+	spin_lock(&env_lock);
 	sched_yield();
 }

diff --git a/kern/sched.c b/kern/sched.c
index 71b60a7..6f0a0b5 100644
--- a/kern/sched.c
+++ b/kern/sched.c
@@ -8,6 +8,8 @@
 void sched_halt(void);

 // Choose a user environment to run and run it.
+//
+// Must be holding env_lock
 void
 sched_yield(void)
 {
@@ -30,25 +32,22 @@ sched_yield(void)
 	// below to halt the cpu.

 	// LAB 4: Your code here.
-	spin_lock(&sched_lock);
+	assert(env_lock.locked);

 	begin = curenv == NULL ? 0 : ENVX(curenv->env_id) + 1;
 	cur = begin;

 	do {
 		if (envs[cur].env_status == ENV_RUNNABLE) {
-			spin_unlock(&sched_lock);
 			env_run(envs + cur);
 		}
 		cur = (cur + 1) % NENV;
 	} while (cur != begin);

 	if (curenv && envs[ENVX(curenv->env_id)].env_status == ENV_RUNNING) {
-		spin_unlock(&sched_lock);
 		env_run(curenv);
 	}

-	spin_unlock(&sched_lock);
 	// sched_halt never returns
 	sched_halt();
 }
@@ -56,11 +55,13 @@ sched_yield(void)
 // Halt this CPU when there is nothing to do. Wait until the
 // timer interrupt wakes it up. This function never returns.
 //
+// Must be holding env_lock
 void
 sched_halt(void)
 {
 	int i;

+	assert(env_lock.locked);
 	// For debugging and testing purposes, if there are no runnable
 	// environments in the system, then drop into the kernel monitor.
 	for (i = 0; i < NENV; i++) {
@@ -69,6 +70,8 @@ sched_halt(void)
 		     envs[i].env_status == ENV_DYING))
 			break;
 	}
+	// Note: at this point we don't need to hold env_lock, but unlocking
+	//  will cause other CPUS to also drop into the kernel monitor.
 	if (i == NENV) {
 		cprintf("No runnable environments in the system!\n");
 		while (1)
@@ -86,6 +89,7 @@ sched_halt(void)

 	// Release the big kernel lock as if we were "leaving" the kernel
 	// unlock_kernel();
+	spin_unlock(&env_lock);

 	// Reset stack pointer, enable interrupts and then halt.
 	asm volatile (
diff --git a/kern/syscall.c b/kern/syscall.c
index b7f7e91..e0d3891 100644
--- a/kern/syscall.c
+++ b/kern/syscall.c
@@ -12,6 +12,7 @@
 #include <kern/syscall.h>
 #include <kern/console.h>
 #include <kern/sched.h>
+#include <kern/spinlock.h>

 // Print a string to the system console.
 // The string is exactly 'len' characters long.
@@ -55,13 +56,17 @@ sys_env_destroy(envid_t envid)
 	int r;
 	struct Env *e;

-	if ((r = envid2env(envid, &e, 1)) < 0)
+	spin_lock(&env_lock);
+	if ((r = _envid2env(envid, &e, 1)) < 0) {
+		spin_unlock(&env_lock);
 		return r;
+	}
 	if (e == curenv)
 		cprintf("[%08x] exiting gracefully\n", curenv->env_id);
 	else
 		cprintf("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
-	env_destroy(e);
+	_env_destroy(e);
+	spin_unlock(&env_lock);
 	return 0;
 }

@@ -69,6 +74,7 @@ sys_env_destroy(envid_t envid)
 static void
 sys_yield(void)
 {
+	spin_lock(&env_lock);
 	sched_yield();
 }

@@ -89,12 +95,15 @@ sys_exofork(void)
 	struct Env *env;
 	int err;

-	if ((err = env_alloc(&env, curenv->env_id)) < 0) {
+	spin_lock(&env_lock);
+	if ((err = _env_alloc(&env, curenv->env_id)) < 0) {
+		spin_unlock(&env_lock);
 		return err;
 	}
 	env->env_status = ENV_NOT_RUNNABLE;
 	env->env_tf = curenv->env_tf;
 	env->env_tf.tf_regs.reg_eax = 0;
+	spin_unlock(&env_lock);

 	return env->env_id;
 }
@@ -119,7 +128,8 @@ sys_env_set_status(envid_t envid, int status)
 	struct Env *env;
 	int err;

-	if ((err = envid2env(envid, &env, 1)) < 0) {
+	spin_lock(&env_lock);
+	if ((err = _envid2env(envid, &env, 1)) < 0) {
 		return err;
 	}

@@ -132,6 +142,8 @@ sys_env_set_status(envid_t envid, int status)
 	}

 	env->env_status = status;
+	spin_unlock(&env_lock);
+
 	return 0;
 }

@@ -342,36 +354,46 @@ sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
 	pte_t *pte;
 	struct PageInfo *pp;

-	if ((r = envid2env(envid, &env, 0)) < 0) {
+	spin_lock(&env_lock);
+	if ((r = _envid2env(envid, &env, 0)) < 0) {
+		spin_unlock(&env_lock);
 		return r;
 	}
 	if (!env->env_ipc_recving) {
+		spin_unlock(&env_lock);
 		return -E_IPC_NOT_RECV;
 	}
 	env->env_ipc_recving = 0;
 	env->env_ipc_from = curenv->env_id;
 	env->env_ipc_value = value;
+	env->env_tf.tf_regs.reg_eax = 0;
 	if ((uintptr_t) srcva < UTOP) {
 		if (PGOFF(srcva) != 0) {
+			spin_unlock(&env_lock);
 			return -E_INVAL;
 		}
 		if ((perm & PTE_SYSCALL) != perm) {
+			spin_unlock(&env_lock);
 			return -E_INVAL;
 		}
 		if ((pp = page_lookup(curenv->env_pgdir, srcva, &pte)) == NULL) {
+			spin_unlock(&env_lock);
 			return -E_INVAL;
 		}
 		if ((perm & PTE_W) && !(*pte & PTE_W)) {
+			spin_unlock(&env_lock);
 			return -E_INVAL;
 		}
 		if ((uintptr_t) env->env_ipc_dstva < UTOP) {
 			if ((r = page_insert(env->env_pgdir, pp, env->env_ipc_dstva, perm)) != 0) {
+				spin_unlock(&env_lock);
 				return r;
 			}
 			env->env_ipc_perm = perm;
 		}
 	}
 	env->env_status = ENV_RUNNABLE;
+	spin_unlock(&env_lock);
 	return 0;
 }

@@ -390,14 +412,17 @@ static int
 sys_ipc_recv(void *dstva)
 {
 	// LAB 4: Your code here.
+	spin_lock(&env_lock);
 	if ((uintptr_t) dstva < UTOP) {
 		if (PGOFF(dstva) != 0) {
+			spin_unlock(&env_lock);
 			return -E_INVAL;
 		}
 		curenv->env_ipc_dstva = dstva;
 	}
 	curenv->env_ipc_recving = 1;
 	curenv->env_status = ENV_NOT_RUNNABLE;
+	spin_unlock(&env_lock);

 	return 0;
 }
@@ -410,37 +435,52 @@ syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
 	// Return any appropriate return value.
 	// LAB 3: Your code here.

+	int32_t ret = 0;
+
 	switch (syscallno) {
 		case SYS_cputs:
 			sys_cputs((const char *)a1, (size_t) a2);
-			return 0;
+			break;
 		case SYS_cgetc:
-			return sys_cgetc();
+			ret = sys_cgetc();
+			break;
 		case SYS_getenvid:
-			return sys_getenvid();
+			ret = sys_getenvid();
+			break;
 		case SYS_env_destroy:
-			return sys_env_destroy(a1);
+			ret = sys_env_destroy(a1);
+			break;
 		case SYS_page_alloc:
-			return sys_page_alloc((envid_t) a1, (void *) a2, (int) a3);
+			ret = sys_page_alloc((envid_t) a1, (void *) a2, (int) a3);
+			break;
 		case SYS_page_map:
-			return sys_page_map((envid_t) a1, (void *) a2, (envid_t) a3, (void *) a4, (int) a5);
+			ret = sys_page_map((envid_t) a1, (void *) a2, (envid_t) a3, (void *) a4, (int) a5);
+			break;
 		case SYS_page_unmap:
-			return sys_page_unmap((envid_t) a1, (void *) a2);
+			ret = sys_page_unmap((envid_t) a1, (void *) a2);
+			break;
 		case SYS_exofork:
-			return sys_exofork();
+			ret = sys_exofork();
+			break;
 		case SYS_env_set_status:
-			return sys_env_set_status((envid_t) a1, (int) a2);
+			ret = sys_env_set_status((envid_t) a1, (int) a2);
+			break;
 		case SYS_env_set_pgfault_upcall:
-			return sys_env_set_pgfault_upcall((envid_t) a1, (void *) a2);
+			ret = sys_env_set_pgfault_upcall((envid_t) a1, (void *) a2);
+			break;
 		case SYS_yield:
 			sys_yield();
-			return 0;
+			break;
 		case SYS_ipc_try_send:
-			return sys_ipc_try_send((envid_t) a1, (uint32_t) a2, (void *) a3, (int) a4);
+			ret = sys_ipc_try_send((envid_t) a1, (uint32_t) a2, (void *) a3, (int) a4);
+			break;
 		case SYS_ipc_recv:
-			return sys_ipc_recv((void *) a1);
+			ret = sys_ipc_recv((void *) a1);
+			break;
 		default:
-			return -E_INVAL;
+			ret = -E_INVAL;
+			break;
 	}
+	return ret;
 }

diff --git a/kern/trap.c b/kern/trap.c
index 26e64bc..4722b67 100644
--- a/kern/trap.c
+++ b/kern/trap.c
@@ -191,7 +191,10 @@ trap_dispatch(struct Trapframe *tf)
 			return;
 		case T_DEBUG:
 		case T_BRKPT:
+			// TODO: check if this is valid
+			spin_lock(&env_lock);
 			monitor(tf);
+			spin_unlock(&env_lock);
 			return;
 		case T_SYSCALL:
 			tf->tf_regs.reg_eax = syscall(
@@ -219,6 +222,7 @@ trap_dispatch(struct Trapframe *tf)
 	// LAB 4: Your code here.
 	switch (tf->tf_trapno) {
 		case IRQ_OFFSET + IRQ_TIMER:
+			spin_lock(&env_lock);
 			lapic_eoi();
 			sched_yield();
 			return;
@@ -269,6 +273,7 @@ trap(struct Trapframe *tf)
 		if (curenv->env_status == ENV_DYING) {
 			env_free(curenv);
 			curenv = NULL;
+			spin_lock(&env_lock);
 			sched_yield();
 		}

@@ -290,10 +295,12 @@ trap(struct Trapframe *tf)
 	// If we made it to this point, then no other environment was
 	// scheduled, so we should return to the current environment
 	// if doing so makes sense.
-	if (curenv && curenv->env_status == ENV_RUNNING)
+	spin_lock(&env_lock);
+	if (curenv && curenv->env_status == ENV_RUNNING) {
 		env_run(curenv);
-	else
+	} else {
 		sched_yield();
+	}
 }


@@ -311,7 +318,12 @@ page_fault_handler(struct Trapframe *tf)
 	// LAB 3: Your code here.
 	if ((tf->tf_cs & 3) == 0) {
 		print_trapframe(tf);
-		panic("Page fault at kernel: %p\n", fault_va);
+		// panic("Page fault at kernel: %p\n", fault_va);
+		cprintf("kernel panic op CPU %d at %s:%d: page fault at kernel: %p\n",
+			cpunum(), __FILE__, __LINE__, fault_va);
+		while (1) {
+			monitor(tf);
+		}
 	}

 	// We've already handled kernel-mode exceptions, so if we get here,
@@ -364,6 +376,7 @@ page_fault_handler(struct Trapframe *tf)
 		utf->utf_esp = tf->tf_esp;
 		tf->tf_esp = (uint32_t) utf;
 		tf->tf_eip = (uint32_t) curenv->env_pgfault_upcall;
+		spin_lock(&env_lock);
 		env_run(curenv);
 	}
```

### References

- [6.828 - Lab4 Challenge 1：Fine-grained Kernel Locking](https://zhuanlan.zhihu.com/p/365252547)
- [add new locks with bkl removed](https://github.com/Kirhhoff/jos/commit/af092ad74ebb774f20518d621e40fa8e868f6530)

[^1]: The check is done by `assert(lock.locked)`. Better using
`kern/spinlock.c:holding()`.
