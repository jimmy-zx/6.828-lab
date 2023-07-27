# Questions

1. `mpentry.S` is linked at high address but loaded at low address. The macro is required to manually convert high address to low address.

2. During interrupt handling, the cpu will push some data before calling the interrupt handler, which might corrupt the stack.

3. The VA sapce of all envs is identical above UTOP (except UVPT), which includes the kernel address space.

4. The environment switch should be transparent to the user so that the user does not need extra handling. The registers are saved by the trap handler and restored by `env_pop_tf()`.
