
Disassembly of section .data:

0000000000000000 <.data>:
   0:	endbr64
   4:	mov    %rdi,%rdx
   7:	mov    %rsi,%r9
   a:	mov    %r9,%r8
   d:	cmp    %rdx,%r8
  10:	setb   %r8b
  14:	movzbl %r8b,%r8d
  18:	test   %r8,%r8
  1b:	jne    0x28
  21:	xor    %r9,%rdx
  24:	mov    %rdx,%rax
  27:	ret
  28:	mov    $0x2,%r8d
  2e:	imul   %r9,%r8
  32:	add    %r8,%rdx
  35:	mov    %rdx,%rax
  38:	ret
