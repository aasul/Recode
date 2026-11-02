
Disassembly of section .data:

0000000000000000 <.data>:
   0:	endbr64
   4:	sub    $0x40,%rsp
   b:	mov    %rcx,%r9
   e:	mov    %rsi,0x8(%rsp)
  13:	movabs $0x8,%rax
  1d:	mov    %rax,0x10(%rsp)
  22:	mov    $0x2,%r10d
  28:	mov    %r9,%rcx
  2b:	cmp    %rcx,%rax
  2e:	ja     0x102
  34:	sub    %rax,%rcx
  37:	cmp    $0x1,%rcx
  3b:	jb     0x102
  41:	movzbl (%rdx,%rax,1),%eax
  45:	mov    %rax,0x18(%rsp)
  4a:	test   %rax,%rax
  4d:	jne    0x6f
  53:	movabs $0x0,%rax
  5d:	mov    %rax,0x0(%rsp)
  62:	mov    %rax,(%r8)
  65:	xor    %eax,%eax
  67:	add    $0x40,%rsp
  6e:	ret
  6f:	movabs $0x0,%rax
  79:	mov    %rax,0x10(%rsp)
  7e:	mov    $0x7,%r10d
  84:	mov    %r9,%rcx
  87:	cmp    %rcx,%rax
  8a:	ja     0x102
  90:	sub    %rax,%rcx
  93:	cmp    $0x8,%rcx
  97:	jb     0x102
  9d:	mov    (%rdx,%rax,1),%rax
  a1:	mov    %rax,0x0(%rsp)
  a6:	mov    0x8(%rsp),%rax
  ab:	mov    0x0(%rsp),%rcx
  b0:	cmp    %rcx,%rax
  b3:	setb   %al
  b6:	movzbl %al,%eax
  b9:	mov    %rax,0x18(%rsp)
  be:	test   %rax,%rax
  c1:	jne    0xe3
  c7:	movabs $0x0,%rax
  d1:	mov    %rax,0x0(%rsp)
  d6:	mov    %rax,(%r8)
  d9:	xor    %eax,%eax
  db:	add    $0x40,%rsp
  e2:	ret
  e3:	mov    0x0(%rsp),%rax
  e8:	mov    0x8(%rsp),%rcx
  ed:	add    %rcx,%rax
  f0:	mov    %rax,0x0(%rsp)
  f5:	mov    %rax,(%r8)
  f8:	xor    %eax,%eax
  fa:	add    $0x40,%rsp
 101:	ret
 102:	mov    %r10,0x8(%r8)
 106:	mov    $0x10,%eax
 10b:	add    $0x40,%rsp
 112:	ret
