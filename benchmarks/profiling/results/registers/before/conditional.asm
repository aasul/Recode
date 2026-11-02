
Disassembly of section .data:

0000000000000000 <.data>:
   0:	endbr64
   4:	sub    $0x40,%rsp
   b:	mov    %rdi,0x0(%rsp)
  10:	mov    %rsi,0x8(%rsp)
  15:	mov    0x8(%rsp),%rax
  1a:	mov    0x0(%rsp),%rcx
  1f:	cmp    %rcx,%rax
  22:	setb   %al
  25:	movzbl %al,%eax
  28:	mov    %rax,0x10(%rsp)
  2d:	test   %rax,%rax
  30:	jne    0x50
  36:	mov    0x0(%rsp),%rax
  3b:	mov    0x8(%rsp),%rcx
  40:	xor    %rcx,%rax
  43:	mov    %rax,0x0(%rsp)
  48:	add    $0x40,%rsp
  4f:	ret
  50:	movabs $0x2,%rax
  5a:	mov    %rax,0x10(%rsp)
  5f:	mov    0x8(%rsp),%rax
  64:	mov    0x10(%rsp),%rcx
  69:	imul   %rcx,%rax
  6d:	mov    %rax,0x10(%rsp)
  72:	mov    0x0(%rsp),%rax
  77:	mov    0x10(%rsp),%rcx
  7c:	add    %rcx,%rax
  7f:	mov    %rax,0x0(%rsp)
  84:	add    $0x40,%rsp
  8b:	ret
