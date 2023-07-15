# Homework: xv6 lazy page allocation

```c
diff --git a/sysproc.c b/sysproc.c
index 0686d29..f8d49bc 100644
--- a/sysproc.c
+++ b/sysproc.c
@@ -51,8 +51,7 @@ sys_sbrk(void)
   if(argint(0, &n) < 0)
     return -1;
   addr = myproc()->sz;
-  if(growproc(n) < 0)
-    return -1;
+  myproc()->sz += n;
   return addr;
 }
 
diff --git a/trap.c b/trap.c
index 41c66eb..7ae4cfd 100644
--- a/trap.c
+++ b/trap.c
@@ -8,6 +8,8 @@
 #include "traps.h"
 #include "spinlock.h"
 
+extern int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);
+
 // Interrupt descriptor table (shared by all CPUs).
 struct gatedesc idt[256];
 extern uint vectors[];  // in vectors.S: array of 256 entry pointers
@@ -36,6 +38,8 @@ idtinit(void)
 void
 trap(struct trapframe *tf)
 {
+  char *mem;
+
   if(tf->trapno == T_SYSCALL){
     if(myproc()->killed)
       exit();
@@ -77,7 +81,21 @@ trap(struct trapframe *tf)
             cpuid(), tf->cs, tf->eip);
     lapiceoi();
     break;
-
+  case T_PGFLT:
+    mem = kalloc();
+    if (mem == 0) {
+      cprintf("trap: T_PGFLT: kalloc: failed\n");
+      myproc()->killed = 1;
+      break;
+    }
+    memset(mem, 0, PGSIZE);
+    if (mappages(myproc()->pgdir, (char *)PGROUNDDOWN(rcr2()), PGSIZE, V2P(mem), PTE_W|PTE_U) < 0) {
+      cprintf("trap: T_PGFLT: mappages: failed\n");
+      kfree(mem);
+      myproc()->killed = 1;
+      break;
+    }
+    break;
   //PAGEBREAK: 13
   default:
     if(myproc() == 0 || (tf->cs&3) == 0){
diff --git a/vm.c b/vm.c
index 7134cff..57f078b 100644
--- a/vm.c
+++ b/vm.c
@@ -57,7 +57,7 @@ walkpgdir(pde_t *pgdir, const void *va, int alloc)
 // Create PTEs for virtual addresses starting at va that refer to
 // physical addresses starting at pa. va and size might not
 // be page-aligned.
-static int
+int
 mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
 {
   char *a, *last;
@@ -233,13 +233,11 @@ allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
   for(; a < newsz; a += PGSIZE){
     mem = kalloc();
     if(mem == 0){
-      cprintf("allocuvm out of memory\n");
       deallocuvm(pgdir, newsz, oldsz);
       return 0;
     }
     memset(mem, 0, PGSIZE);
     if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
-      cprintf("allocuvm out of memory (2)\n");
       deallocuvm(pgdir, newsz, oldsz);
       kfree(mem);
       return 0;
```
