# Questions

1. `mpentry.S` is linked at high address but loaded at low address. The macro is required to manually convert high address to low address.

2. During interrupt handling, the cpu will push some data before calling the interrupt handler, which might corrupt the stack.
