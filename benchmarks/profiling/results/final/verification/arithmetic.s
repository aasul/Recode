.text
endbr64
mov %rdi,%rdx
mov %rsi,%r9
mov $2,%r8d
imul %r9,%r8
add %r8,%rdx
mov %rdx,%rax
ret
.section .note.GNU-stack,"",@progbits
