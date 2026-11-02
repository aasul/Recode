
Disassembly of section .data:

0000000000000000 <.data>:
   0:	endbr64
   4:	sub    $0x20,%rsp
   b:	mov    %rcx,%r9
   e:	mov    %rsi,%rax
  11:	mov    %rax,0x8(%rsp)
  16:	mov    $0x8,%edi
  1b:	mov    $0x2,%r10d
  21:	mov    %rdi,%rax
  24:	mov    %r9,%rcx
  27:	cmp    %rcx,%rax
  2a:	ja     0xe7
  30:	sub    %rax,%rcx
  33:	cmp    $0x1,%rcx
  37:	jb     0xe7
  3d:	movzbl (%rdx,%rax,1),%eax
  41:	mov    %rax,0x18(%rsp)
  46:	mov    0x18(%rsp),%rax
  4b:	test   %rax,%rax
  4e:	jne    0x6a
  54:	mov    $0x0,%r11d
  5a:	mov    %r11,%rax
  5d:	mov    %rax,(%r8)
  60:	xor    %eax,%eax
  62:	add    $0x20,%rsp
  69:	ret
  6a:	mov    $0x0,%edi
  6f:	mov    $0x7,%r10d
  75:	mov    %rdi,%rax
  78:	mov    %r9,%rcx
  7b:	cmp    %rcx,%rax
  7e:	ja     0xe7
  84:	sub    %rax,%rcx
  87:	cmp    $0x8,%rcx
  8b:	jb     0xe7
  91:	mov    (%rdx,%rax,1),%rax
  95:	mov    %rax,%r11
  98:	mov    0x8(%rsp),%rax
  9d:	cmp    %r11,%rax
  a0:	setb   %al
  a3:	movzbl %al,%eax
  a6:	mov    %rax,0x18(%rsp)
  ab:	mov    0x18(%rsp),%rax
  b0:	test   %rax,%rax
  b3:	jne    0xcf
  b9:	mov    $0x0,%r11d
  bf:	mov    %r11,%rax
  c2:	mov    %rax,(%r8)
  c5:	xor    %eax,%eax
  c7:	add    $0x20,%rsp
  ce:	ret
  cf:	mov    0x8(%rsp),%rcx
  d4:	add    %rcx,%r11
  d7:	mov    %r11,%rax
  da:	mov    %rax,(%r8)
  dd:	xor    %eax,%eax
  df:	add    $0x20,%rsp
  e6:	ret
  e7:	mov    %r10,0x8(%r8)
  eb:	mov    $0x10,%eax
  f0:	add    $0x20,%rsp
  f7:	ret
