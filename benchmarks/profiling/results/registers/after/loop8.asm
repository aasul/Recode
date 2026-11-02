
Disassembly of section .data:

0000000000000000 <.data>:
   0:	endbr64
   4:	mov    %rdi,%rdx
   7:	mov    %rsi,%r9
   a:	mov    $0x8,%r8d
  10:	mov    $0x1,%r10d
  16:	mov    $0x3,%r11d
  1c:	xor    %r9,%rdx
  1f:	imul   %r11,%rdx
  23:	sub    %r10,%r8
  26:	test   %r8,%r8
  29:	jne    0x1c
  2f:	mov    %rdx,%rax
  32:	ret
