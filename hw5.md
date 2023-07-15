# Homework: xv6 CPU alarm

[Ref](https://zhuanlan.zhihu.com/p/365225848)

## Appendix: Diff

```c
diff --git a/proc.c b/proc.c
index 806b1b1..a447d70 100644
--- a/proc.c
+++ b/proc.c
@@ -112,6 +112,10 @@ found:
   memset(p->context, 0, sizeof *p->context);
   p->context->eip = (uint)forkret;
 
+  p->alarmticks = -1;
+  p->alarmhandler = 0;
+  p->alarmlasttrigger = -1;
+
   return p;
 }
 
diff --git a/proc.h b/proc.h
index 1647114..73a2fba 100644
--- a/proc.h
+++ b/proc.h
@@ -49,6 +49,9 @@ struct proc {
   struct file *ofile[NOFILE];  // Open files
   struct inode *cwd;           // Current directory
   char name[16];               // Process name (debugging)
+  int alarmticks;
+  void (*alarmhandler)();
+  int alarmlasttrigger;
 };
 
 // Process memory is laid out contiguously, low addresses first:
diff --git a/syscall.c b/syscall.c
index ee85261..4f57474 100644
--- a/syscall.c
+++ b/syscall.c
@@ -103,6 +103,7 @@ extern int sys_unlink(void);
 extern int sys_wait(void);
 extern int sys_write(void);
 extern int sys_uptime(void);
+extern int sys_alarm(void);
 
 static int (*syscalls[])(void) = {
 [SYS_fork]    sys_fork,
@@ -126,6 +127,7 @@ static int (*syscalls[])(void) = {
 [SYS_link]    sys_link,
 [SYS_mkdir]   sys_mkdir,
 [SYS_close]   sys_close,
+[SYS_alarm]   sys_alarm,
 };
 
 void
diff --git a/syscall.h b/syscall.h
index bc5f356..7f2e507 100644
--- a/syscall.h
+++ b/syscall.h
@@ -20,3 +20,4 @@
 #define SYS_link   19
 #define SYS_mkdir  20
 #define SYS_close  21
+#define SYS_alarm  22
diff --git a/sysproc.c b/sysproc.c
index 0686d29..04c2bca 100644
--- a/sysproc.c
+++ b/sysproc.c
@@ -89,3 +89,21 @@ sys_uptime(void)
   release(&tickslock);
   return xticks;
 }
+
+int
+sys_alarm(void) {
+  int ntick;
+  void (*handler)();
+  if (argint(0, &ntick) < 0) {
+    return -1;
+  }
+  if (argptr(1, (char**)&handler, 1) < 0) {
+    return -1;
+  }
+  myproc()->alarmticks = ntick;
+  myproc()->alarmhandler = handler;
+  acquire(&tickslock);
+  myproc()->alarmlasttrigger = ticks;
+  release(&tickslock);
+  return 0;
+}
diff --git a/trap.c b/trap.c
index 41c66eb..3e4f81b 100644
--- a/trap.c
+++ b/trap.c
@@ -52,6 +52,17 @@ trap(struct trapframe *tf)
       acquire(&tickslock);
       ticks++;
       wakeup(&ticks);
+      if (myproc() != 0 && (tf->cs & 3) == 3) {  // user process
+        if (myproc()->alarmticks != -1) {  // alarm enabled
+          if (ticks >= myproc()->alarmlasttrigger + myproc()->alarmticks) {
+            myproc()->alarmlasttrigger = ticks;
+            // `call alarmhandler`
+            tf->esp -= 4;
+            *((uint*)tf->esp) = tf->eip;
+            tf->eip = (uint)myproc()->alarmhandler;
+          }
+        }
+      }
       release(&tickslock);
     }
     lapiceoi();
diff --git a/user.h b/user.h
index 4f99c52..015073b 100644
--- a/user.h
+++ b/user.h
@@ -23,6 +23,7 @@ int getpid(void);
 char* sbrk(int);
 int sleep(int);
 int uptime(void);
+int alarm(int, void (*)());
 
 // ulib.c
 int stat(const char*, struct stat*);
diff --git a/usys.S b/usys.S
index 8bfd8a1..a12e199 100644
--- a/usys.S
+++ b/usys.S
@@ -29,3 +29,4 @@ SYSCALL(getpid)
 SYSCALL(sbrk)
 SYSCALL(sleep)
 SYSCALL(uptime)
+SYSCALL(alarm)
```
