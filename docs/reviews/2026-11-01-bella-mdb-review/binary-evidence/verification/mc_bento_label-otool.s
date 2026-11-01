00000000000dda24	sub	sp, sp, #0x70
00000000000dda28	stp	x24, x23, [sp, #0x30]
00000000000dda2c	stp	x22, x21, [sp, #0x40]
00000000000dda30	stp	x20, x19, [sp, #0x50]
00000000000dda34	stp	x29, x30, [sp, #0x60]
00000000000dda38	add	x29, sp, #0x60
00000000000dda3c	mov	x20, x3
00000000000dda40	mov	x22, x2
00000000000dda44	mov	x19, x1
00000000000dda48	mov	x23, x0
00000000000dda4c	cbnz	x1, 0xdda78
00000000000dda50	adrp	x0, 3814 ; 0xfc3000
00000000000dda54	add	x0, x0, #0x45b ; literal pool for: "(strm)!=0"
00000000000dda58	adrp	x1, 3814 ; 0xfc3000
00000000000dda5c	add	x1, x1, #0x2e9 ; literal pool for: "/Volumes/Media/Builds/workspace/MC/MCRelease_Installer/Mac_Build/coresw/ame/src/ameOMFI/omfiHPDomain.c"
00000000000dda60	mov	w2, #0x1cc
00000000000dda64	bl	0xe8f1cc ; symbol stub for: __Z17__FtlAssertFailedPKcS0_i
00000000000dda68	cbz	w0, 0xdda78
00000000000dda6c	adrp	x0, 3791 ; 0xfac000
00000000000dda70	add	x0, x0, #0xca1 ; literal pool for: "\021FtlAssert called."
00000000000dda74	bl	0xe8e3d4 ; symbol stub for: _DebugStr
00000000000dda78	mov	x0, x19
00000000000dda7c	bl	0xe92958 ; symbol stub for: __ZN7AStream13IsSwapBytesOnEv
00000000000dda80	mov	x21, x0
00000000000dda84	ldrb	w8, [x23, #0x3c]
00000000000dda88	cmp	w8, #0x0
00000000000dda8c	cset	w1, ne
00000000000dda90	mov	x0, x19
00000000000dda94	bl	0xe92940 ; symbol stub for: __ZN7AStream12SetSwapBytesEh
00000000000dda98	ldr	x8, [x19]
00000000000dda9c	ldr	x8, [x8, #0x98]
00000000000ddaa0	mov	x0, x19
00000000000ddaa4	blr	x8
00000000000ddaa8	cmp	w0, #0x17
00000000000ddaac	b.gt	0xddad4
00000000000ddab0	str	xzr, [sp]
00000000000ddab4	adrp	x1, 3814 ; 0xfc3000
00000000000ddab8	add	x1, x1, #0x465 ; literal pool for: "omfiHPDomain_BAD_MAGIC"
00000000000ddabc	add	x0, sp, #0x8
00000000000ddac0	bl	0xe92784 ; symbol stub for: __ZN6XErrorC1EPKcz
00000000000ddac4	add	x0, sp, #0x8
00000000000ddac8	bl	0xe94fbc ; symbol stub for: __ZpsRK6XError
00000000000ddacc	add	x0, sp, #0x8
00000000000ddad0	bl	0xe9279c ; symbol stub for: __ZN6XErrorD1Ev
00000000000ddad4	mov	x0, x19
00000000000ddad8	bl	__Z10IsOMFIFileP7AStream
00000000000ddadc	cbnz	w0, 0xddb04
00000000000ddae0	str	xzr, [sp]
00000000000ddae4	adrp	x1, 3814 ; 0xfc3000
00000000000ddae8	add	x1, x1, #0x465 ; literal pool for: "omfiHPDomain_BAD_MAGIC"
00000000000ddaec	add	x0, sp, #0x8
00000000000ddaf0	bl	0xe92784 ; symbol stub for: __ZN6XErrorC1EPKcz
00000000000ddaf4	add	x0, sp, #0x8
00000000000ddaf8	bl	0xe94fbc ; symbol stub for: __ZpsRK6XError
00000000000ddafc	add	x0, sp, #0x8
00000000000ddb00	bl	0xe9279c ; symbol stub for: __ZN6XErrorD1Ev
00000000000ddb04	ldr	x8, [x19]
00000000000ddb08	ldr	x8, [x8, #0x88]
00000000000ddb0c	mov	x0, x19
00000000000ddb10	mov	w1, #0x2
00000000000ddb14	blr	x8
00000000000ddb18	ldr	x8, [x19]
00000000000ddb1c	ldr	x8, [x8, #0x230]
00000000000ddb20	adrp	x1, 3814 ; 0xfc3000
00000000000ddb24	add	x1, x1, #0x47c ; literal pool for: "byte encoding"
00000000000ddb28	mov	x0, x19
00000000000ddb2c	blr	x8
00000000000ddb30	strh	w0, [x23, #0x3e]
00000000000ddb34	ldr	x8, [x19]
00000000000ddb38	ldr	x8, [x8, #0x230]
00000000000ddb3c	adrp	x1, 3814 ; 0xfc3000
00000000000ddb40	add	x1, x1, #0x48a ; literal pool for: "Bento Major Version"
00000000000ddb44	mov	x0, x19
00000000000ddb48	blr	x8
00000000000ddb4c	mov	x23, x0
00000000000ddb50	ldr	x8, [x19]
00000000000ddb54	ldr	x8, [x8, #0x230]
00000000000ddb58	adrp	x1, 3814 ; 0xfc3000
00000000000ddb5c	add	x1, x1, #0x49e ; literal pool for: "Bento Minor Version"
00000000000ddb60	mov	x0, x19
00000000000ddb64	blr	x8
00000000000ddb68	cmp	w23, #0x1
00000000000ddb6c	ccmp	w0, #0x0, #0x0, eq
00000000000ddb70	b.eq	0xddb98
00000000000ddb74	str	xzr, [sp]
00000000000ddb78	adrp	x1, 3814 ; 0xfc3000
00000000000ddb7c	add	x1, x1, #0x4b2 ; literal pool for: "omfiHPDomain_UNKNOWN_VERSION"
00000000000ddb80	add	x0, sp, #0x8
00000000000ddb84	bl	0xe92784 ; symbol stub for: __ZN6XErrorC1EPKcz
00000000000ddb88	add	x0, sp, #0x8
00000000000ddb8c	bl	0xe94fbc ; symbol stub for: __ZpsRK6XError
00000000000ddb90	add	x0, sp, #0x8
00000000000ddb94	bl	0xe9279c ; symbol stub for: __ZN6XErrorD1Ev
00000000000ddb98	ldr	x8, [x19]
00000000000ddb9c	ldr	x8, [x8, #0x250]
00000000000ddba0	adrp	x1, 3814 ; 0xfc3000
00000000000ddba4	add	x1, x1, #0x4cf ; literal pool for: "Bento TOC Offset"
00000000000ddba8	mov	x0, x19
00000000000ddbac	blr	x8
00000000000ddbb0	str	w0, [x22]
00000000000ddbb4	ldr	x8, [x19]
00000000000ddbb8	ldr	x8, [x8, #0x250]
00000000000ddbbc	adrp	x1, 3814 ; 0xfc3000
00000000000ddbc0	add	x1, x1, #0x4e0 ; literal pool for: "Bento TOC Length"
00000000000ddbc4	mov	x0, x19
00000000000ddbc8	blr	x8
00000000000ddbcc	str	w0, [x20]
00000000000ddbd0	ldr	x8, [x19]
00000000000ddbd4	ldr	x8, [x8, #0xb0]
00000000000ddbd8	mov	x0, x19
00000000000ddbdc	mov	x1, #0x0
00000000000ddbe0	blr	x8
00000000000ddbe4	ldr	x8, [x19]
00000000000ddbe8	ldr	x8, [x8, #0x250]
00000000000ddbec	adrp	x1, 3814 ; 0xfc3000
00000000000ddbf0	add	x1, x1, #0x4f1 ; literal pool for: "init buffer"
00000000000ddbf4	mov	x0, x19
00000000000ddbf8	blr	x8
00000000000ddbfc	mov	x0, x19
00000000000ddc00	mov	x1, x21
00000000000ddc04	bl	0xe92940 ; symbol stub for: __ZN7AStream12SetSwapBytesEh
00000000000ddc08	ldp	x29, x30, [sp, #0x60]
00000000000ddc0c	ldp	x20, x19, [sp, #0x50]
00000000000ddc10	ldp	x22, x21, [sp, #0x40]
00000000000ddc14	ldp	x24, x23, [sp, #0x30]
00000000000ddc18	add	sp, sp, #0x70
00000000000ddc1c	ret
