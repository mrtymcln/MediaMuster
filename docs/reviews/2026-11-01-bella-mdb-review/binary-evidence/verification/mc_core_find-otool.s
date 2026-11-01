000000000006cc7c	sub	sp, sp, #0x90
000000000006cc80	stp	x22, x21, [sp, #0x60]
000000000006cc84	stp	x20, x19, [sp, #0x70]
000000000006cc88	stp	x29, x30, [sp, #0x80]
000000000006cc8c	add	x29, sp, #0x80
000000000006cc90	mov	x20, x3
000000000006cc94	mov	x19, x1
000000000006cc98	mov	x21, x0
000000000006cc9c	stur	x2, [x29, #-0x28]
000000000006cca0	ldr	x8, [x0, #0x18]
000000000006cca4	add	x0, x8, #0x10
000000000006cca8	sub	x1, x29, #0x28
000000000006ccac	bl	__ZNK5boost11multi_index6detail18ordered_index_implINS0_6memberI13AttributeInfoA33_cXadL_ZNS4_4nameEEEEE12attrNameLessNS1_9nth_layerILi2ES4_NS0_10indexed_byINS0_13random_accessINS0_3tagIN4mpl_2naESD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_EEEENS0_14ordered_uniqueINSB_I13attrNameIndexSD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_EES6_S7_EESD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_SD_EENSt3__19allocatorIS4_EEEENS_3mpl6v_itemISH_NSP_7vector0ISD_EELi0EEENS1_18ordered_unique_tagENS1_19null_augment_policyEE4findIPKcEENS1_19bidir_node_iteratorINS1_18ordered_index_nodeISV_NS1_15index_node_baseIS4_SN_EEEEEERKT_
000000000006ccb0	ldr	x8, [x21, #0x18]
000000000006ccb4	ldr	x8, [x8, #0x8]
000000000006ccb8	cmp	x8, x0
000000000006ccbc	b.eq	0x6ccdc
000000000006ccc0	ldr	w8, [x0]
000000000006ccc4	cbz	x20, 0x6ccec
000000000006ccc8	cmp	w8, w19
000000000006cccc	cset	w8, eq
000000000006ccd0	strb	w8, [x20]
000000000006ccd4	b.eq	0x6ccf4
000000000006ccd8	b	0x6cd3c
000000000006ccdc	cbz	x20, 0x6ccfc
000000000006cce0	mov	x0, #0x0
000000000006cce4	strb	wzr, [x20]
000000000006cce8	b	0x6cd40
000000000006ccec	cmp	w8, w19
000000000006ccf0	b.ne	0x6ccfc
000000000006ccf4	ldr	x0, [x0, #0x28]
000000000006ccf8	b	0x6cd40
000000000006ccfc	ldur	x8, [x29, #-0x28]
000000000006cd00	stp	x19, xzr, [sp, #0x18]
000000000006cd04	adrp	x9, 754 ; 0x35e000
000000000006cd08	add	x9, x9, #0x2df ; literal pool for: "int kind"
000000000006cd0c	stp	x8, x9, [sp, #0x8]
000000000006cd10	adrp	x9, 744 ; 0x354000
000000000006cd14	add	x9, x9, #0xff5 ; literal pool for: "str name"
000000000006cd18	str	x9, [sp]
000000000006cd1c	adrp	x1, 754 ; 0x35e000
000000000006cd20	add	x1, x1, #0x2b4 ; literal pool for: "ATTR_NOT_FOUND"
000000000006cd24	add	x0, sp, #0x30
000000000006cd28	bl	__ZN6XErrorC1EPKcz
000000000006cd2c	add	x0, sp, #0x30
000000000006cd30	bl	__ZpsRK6XError
000000000006cd34	add	x0, sp, #0x30
000000000006cd38	bl	__ZN6XErrorD1Ev
000000000006cd3c	mov	x0, #0x0
000000000006cd40	ldp	x29, x30, [sp, #0x80]
000000000006cd44	ldp	x20, x19, [sp, #0x70]
000000000006cd48	ldp	x22, x21, [sp, #0x60]
000000000006cd4c	add	sp, sp, #0x90
000000000006cd50	ret
