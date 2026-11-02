
Disassembly of section .data:

0000000000000000 <.data>:
   0:	endbr64
   4:	sub    $0x40,%rsp
   b:	mov    %rdi,0x0(%rsp)
  10:	mov    %rsi,0x8(%rsp)
  15:	movabs $0x2,%rax
  1f:	mov    %rax,0x10(%rsp)
  24:	mov    0x8(%rsp),%rax
  29:	mov    0x10(%rsp),%rcx
  2e:	imul   %rcx,%rax
  32:	mov    %rax,0x10(%rsp)
  37:	mov    0x0(%rsp),%rax
  3c:	mov    0x10(%rsp),%rcx
  41:	add    %rcx,%rax
  44:	mov    %rax,0x0(%rsp)
  49:	add    $0x40,%rsp
  50:	ret
