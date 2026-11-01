__ZN8omf2JPED12IsInstanceOfEPKc:
00000000004ebbd0	stp	x20, x19, [sp, #-0x20]!
00000000004ebbd4	stp	x29, x30, [sp, #0x10]
00000000004ebbd8	add	x29, sp, #0x10
00000000004ebbdc	mov	x19, x1
00000000004ebbe0	mov	x20, x0
00000000004ebbe4	adrp	x0, 2859 ; 0x1016000
00000000004ebbe8	add	x0, x0, #0x2b1 ; literal pool for: "omf2JPED"
00000000004ebbec	bl	0xe95790 ; symbol stub for: _strcmp
00000000004ebbf0	cbz	w0, 0x4ebc0c
00000000004ebbf4	mov	x0, x20
00000000004ebbf8	mov	x1, x19
00000000004ebbfc	bl	__ZN8omf2DIDD12IsInstanceOfEPKc
00000000004ebc00	cmp	w0, #0x0
00000000004ebc04	cset	w0, ne
00000000004ebc08	b	0x4ebc10
00000000004ebc0c	mov	w0, #0x1
00000000004ebc10	ldp	x29, x30, [sp, #0x10]
00000000004ebc14	ldp	x20, x19, [sp], #0x20
00000000004ebc18	ret
