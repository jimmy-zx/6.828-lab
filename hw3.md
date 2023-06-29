# Homework: xv6 system calls

## Part One

1. Make a table (array) of system call names `static char *syscall_name`.
2. Capture the result of syscall and print out information.

## Part Two

`rg uptime` results in

```c
syscall.h
15:#define SYS_uptime 14

syscall.c
105:extern int sys_uptime(void);
122:[SYS_uptime]  sys_uptime,
147:[SYS_uptime]  "uptime",

sysproc.c
83:sys_uptime(void)

user.h
25:int uptime(void);

usys.S
31:SYSCALL(uptime)
```

### Kernel space

- `syscall.h`: defines syscall number
- `syscall.c`: defines syscall correspondence to implementation (and name)
- `sysproc.c`: implements system call

### User space

- `user.h`: defines user-space syscall prototype
- `usys.S`: defines user-space syscall implementation (emits an interrupt when called)

## Appendix: diff

```c
diff --git a/Makefile b/Makefile
index 09d790c..54c3fb5 100644
--- a/Makefile
+++ b/Makefile
@@ -181,6 +181,7 @@ UPROGS=\
 	_usertests\
 	_wc\
 	_zombie\
+	_date\
 
 fs.img: mkfs README $(UPROGS)
 	./mkfs fs.img README $(UPROGS)
diff --git a/syscall.c b/syscall.c
index ee85261..59c9727 100644
--- a/syscall.c
+++ b/syscall.c
@@ -103,6 +103,7 @@ extern int sys_unlink(void);
 extern int sys_wait(void);
 extern int sys_write(void);
 extern int sys_uptime(void);
+extern int sys_date(void);
 
 static int (*syscalls[])(void) = {
 [SYS_fork]    sys_fork,
@@ -126,6 +127,32 @@ static int (*syscalls[])(void) = {
 [SYS_link]    sys_link,
 [SYS_mkdir]   sys_mkdir,
 [SYS_close]   sys_close,
+[SYS_date]    sys_date,
+};
+
+static char *syscall_name[] = {
+[SYS_fork]    "fork",
+[SYS_exit]    "exit",
+[SYS_wait]    "wait",
+[SYS_pipe]    "pipe",
+[SYS_read]    "read",
+[SYS_kill]    "kill",
+[SYS_exec]    "exec",
+[SYS_fstat]   "fstat",
+[SYS_chdir]   "chdir",
+[SYS_dup]     "dup",
+[SYS_getpid]  "getpid",
+[SYS_sbrk]    "sbrk",
+[SYS_sleep]   "sleep",
+[SYS_uptime]  "uptime",
+[SYS_open]    "open",
+[SYS_write]   "write",
+[SYS_mknod]   "mknod",
+[SYS_unlink]  "unlink",
+[SYS_link]    "link",
+[SYS_mkdir]   "mkdir",
+[SYS_close]   "close",
+[SYS_date]    "date",
 };
 
 void
@@ -136,7 +163,9 @@ syscall(void)
 
   num = curproc->tf->eax;
   if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
-    curproc->tf->eax = syscalls[num]();
+    uint res = syscalls[num]();
+    cprintf("%s -> %d\n", syscall_name[num], res);
+    curproc->tf->eax = res;
   } else {
     cprintf("%d %s: unknown sys call %d\n",
             curproc->pid, curproc->name, num);
diff --git a/syscall.h b/syscall.h
index bc5f356..1a620b9 100644
--- a/syscall.h
+++ b/syscall.h
@@ -20,3 +20,4 @@
 #define SYS_link   19
 #define SYS_mkdir  20
 #define SYS_close  21
+#define SYS_date   22
diff --git a/sysproc.c b/sysproc.c
index 0686d29..7d2db65 100644
--- a/sysproc.c
+++ b/sysproc.c
@@ -89,3 +89,14 @@ sys_uptime(void)
   release(&tickslock);
   return xticks;
 }
+
+int
+sys_date(void)
+{
+  struct rtcdate *r;
+  if (argptr(0, (char **)&r, sizeof(struct rtcdate)) < 0) {
+    return -1;
+  }
+  cmostime(r);
+  return 0;
+}
diff --git a/user.h b/user.h
index 4f99c52..98e509f 100644
--- a/user.h
+++ b/user.h
@@ -23,6 +23,7 @@ int getpid(void);
 char* sbrk(int);
 int sleep(int);
 int uptime(void);
+int date(struct rtcdate *);
 
 // ulib.c
 int stat(const char*, struct stat*);
diff --git a/usys.S b/usys.S
index 8bfd8a1..ba76d54 100644
--- a/usys.S
+++ b/usys.S
@@ -29,3 +29,4 @@ SYSCALL(getpid)
 SYSCALL(sbrk)
 SYSCALL(sleep)
 SYSCALL(uptime)
+SYSCALL(date)
```
