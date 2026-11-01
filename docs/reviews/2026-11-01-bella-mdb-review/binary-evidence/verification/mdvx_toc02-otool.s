0000000100061e48	stp	x20, x19, [sp, #-0x20]!
0000000100061e4c	stp	x29, x30, [sp, #0x10]
0000000100061e50	add	x29, sp, #0x10
0000000100061e54	nop
0000000100061e58	ldr	x19, #0x9aed0 ; Objc class ref: xConsole
0000000100061e5c	nop
0000000100061e60	ldr	x0, #0x9aed0 ; Objc class ref: _OBJC_CLASS_$_NSString
0000000100061e64	adr	x2, #570900 ; Objc cfstring ref: @"bad cfstring ref"
0000000100061e68	nop
0000000100061e6c	bl	"_objc_msgSend$stringWithFormat:"
0000000100061e70	mov	x29, x29
0000000100061e74	bl	0x100069d48 ; symbol stub for: _objc_retainAutoreleasedReturnValue
0000000100061e78	mov	x20, x0
0000000100061e7c	adr	x2, #405533 ; literal pool for: "/Volumes/MyHome/Programming/Xcode_projects/MDVx/MDVx1/MDVxCore/MDVxOMFI.m"
0000000100061e80	nop
0000000100061e84	adr	x4, #405764 ; literal pool for: "-[MDVxOMFI OMFiTOCReader02]"
0000000100061e88	nop
0000000100061e8c	mov	x0, x19
0000000100061e90	mov	w3, #0x16d
0000000100061e94	mov	x5, x20
0000000100061e98	bl	"_objc_msgSend$xNSLog::::"
0000000100061e9c	mov	x0, x20
0000000100061ea0	ldp	x29, x30, [sp, #0x10]
0000000100061ea4	ldp	x20, x19, [sp], #0x20
0000000100061ea8	b	0x100069d18 ; symbol stub for: _objc_release
