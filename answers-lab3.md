# Questions

1. What is the purpose of having an individual handler function for each exception/interrupt?

- Allows the system to distinguish between different traps.
- Allows different parts of one system to handle different traps.
- Allows the user to emit interrupts (otherwise there is only one permission)

2. Did you have to do anything to make the user/softint program behave correctly? The grade script expects it to produce a general protection fault (trap 13), but softint's code says int $14. Why should this produce interrupt vector 13? What happens if the kernel actually allows softint's int $14 instruction to invoke the kernel's page fault handler (which is interrupt vector 14)?

`user/softint` emits `int $14` in user mode (3), which is higher than the installed interrupt gate (0). Therefore the system emits a general protection fault.

This prevents the user from maliciously issue some predefined interrupts to crash the system.
