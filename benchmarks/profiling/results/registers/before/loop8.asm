
Disassembly of section .data:

0000000000000000 <.data>:
   0:	endbr64
   4:	sub    $0x40,%rsp
   b:	mov    %rdi,0x0(%rsp)
  10:	mov    %rsi,0x8(%rsp)
  15:	movabs $0x8,%rax
  1f:	mov    %rax,0x10(%rsp)
  24:	movabs $0x1,%rax
  2e:	mov    %rax,0x18(%rsp)
  33:	movabs $0x3,%rax
  3d:	mov    %rax,0x20(%rsp)
  42:	mov    0x0(%rsp),%rax
  47:	mov    0x8(%rsp),%rcx
  4c:	xor    %rcx,%rax
  4f:	mov    %rax,0x0(%rsp)
  54:	mov    0x20(%rsp),%rcx
  59:	imul   %rcx,%rax
  5d:	mov    %rax,0x0(%rsp)
  62:	mov    0x10(%rsp),%rax
  67:	mov    0x18(%rsp),%rcx
  6c:	sub    %rcx,%rax
  6f:	mov    %rax,0x10(%rsp)
  74:	test   %rax,%rax
  77:	jne    0x42
  7d:	mov    0x0(%rsp),%rax
  82:	add    $0x40,%rsp
  89:	ret
