
Disassembly of section .data:

0000000000000000 <.data>:
   0:	endbr64
   4:	sub    $0x40,%rsp
   b:	mov    %rdi,0x0(%rsp)
  10:	mov    %rsi,0x8(%rsp)
  15:	mov    0x0(%rsp),%rax
  1a:	mov    0x8(%rsp),%rcx
  1f:	and    %rcx,%rax
  22:	mov    %rax,0x10(%rsp)
  27:	mov    0x0(%rsp),%rax
  2c:	mov    0x8(%rsp),%rcx
  31:	or     %rcx,%rax
  34:	mov    %rax,0x18(%rsp)
  39:	mov    0x10(%rsp),%rax
  3e:	mov    0x18(%rsp),%rcx
  43:	add    %rcx,%rax
  46:	mov    %rax,0x10(%rsp)
  4b:	mov    0x0(%rsp),%rax
  50:	mov    0x8(%rsp),%rcx
  55:	sub    %rcx,%rax
  58:	mov    %rax,0x18(%rsp)
  5d:	mov    0x10(%rsp),%rax
  62:	mov    0x18(%rsp),%rcx
  67:	xor    %rcx,%rax
  6a:	mov    %rax,0x0(%rsp)
  6f:	add    $0x40,%rsp
  76:	ret
