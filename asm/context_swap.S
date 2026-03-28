.type get_context, @function
.global get_context
get_context:
    // can't modify rsp or rip directly, 
    // so we move them into r8 and r9 first
    movq (%rsp), %r8
    leaq 8(%rsp), %r9

    // save rip and rsp into context struct
    movq %r8, 8*0(%rdi) 
    movq %r9, 8*1(%rdi)

    // save all other registers into context struct
    movq %rbx, 8*2(%rdi)
    movq %rbp, 8*3(%rdi)
    movq %r12, 8*4(%rdi)
    movq %r13, 8*5(%rdi)
    movq %r14, 8*6(%rdi)
    movq %r15, 8*7(%rdi)

    xorl %eax, %eax
    ret

.type set_context, @function
.global set_context
set_context:
    // set the stack pointer
    movq 8*0(%rdi), %r8

    // since we can't modify rip directly,
    // save for now, we will push at the end
    movq 8*1(%rdi), %rsp

    // set the other registers
    movq 8*2(%rdi), %rbx
    movq 8*3(%rdi), %rbp
    movq 8*4(%rdi), %r12
    movq 8*5(%rdi), %r13
    movq 8*6(%rdi), %r14
    movq 8*7(%rdi), %r15

    // push rip onto stack to return to
    // correct address
    pushq %r8

    xorl %eax, %eax
    ret

.section .note.GNU-stack, "", @progbits