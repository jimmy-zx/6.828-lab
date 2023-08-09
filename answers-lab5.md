# Lab 5: File system, Spawn and Shell

## Questions

1. Do you have to do anything else to ensure that this I/O privilege setting is saved and restored properly when you subsequently switch from one environment to another? Why?

No. x86 interrupt handling will automatically save/restore eflags register.
