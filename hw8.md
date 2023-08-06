# Homework: User-level threads

```assembly
	.text

/* Switch from current_thread to next_thread. Make next_thread
 * the current_thread, and set next_thread to 0.
 * Use eax as a temporary register; it is caller saved.
 */
	.globl thread_switch
thread_switch:
	/* YOUR CODE HERE */

	// save current state
	pushal
	movl current_thread, %eax
	movl %esp, (%eax)

	// current_thread = next_thread
	movl next_thread, %eax
	movl %eax, current_thread

	// next_thread = 0
	movl $0, next_thread

	// restore previous state
	movl current_thread, %eax
	movl (%eax), %esp
	popal

	ret				/* pop return address from stack */
```
