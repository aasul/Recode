.text
.globl rc_test_integer_abi
.type rc_test_integer_abi,@function
rc_test_integer_abi:
    endbr64
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    sub $24,%rsp
    mov %rcx,(%rsp)
    mov %rdi,%rax
    mov %rsi,%rdi
    mov %rdx,%rsi
    mov $101,%rbp
    mov $102,%rbx
    mov $103,%r12
    mov $104,%r13
    mov $105,%r14
    mov $106,%r15
    call *%rax
    mov $0,%edx
    cmp $101,%rbp
    jne 1f
    cmp $102,%rbx
    jne 1f
    cmp $103,%r12
    jne 1f
    cmp $104,%r13
    jne 1f
    cmp $105,%r14
    jne 1f
    cmp $106,%r15
    jne 1f
    mov $1,%edx
1:
    mov (%rsp),%rcx
    mov %rdx,(%rcx)
    add $24,%rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret
.size rc_test_integer_abi,.-rc_test_integer_abi
.section .note.GNU-stack,"",@progbits
