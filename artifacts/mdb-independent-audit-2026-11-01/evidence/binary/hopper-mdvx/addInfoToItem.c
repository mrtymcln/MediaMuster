    r31 = r31 - 0x80;
    var_50 = r28;
    decomp_var_48 = r27;
    var_40 = r26;
    decomp_var_38 = r25;
    var_30 = r24;
    decomp_var_28 = r23;
    var_20 = r22;
    decomp_var_18 = r21;
    var_10 = r20;
    decomp_var_8 = r19;
    saved_fp = r29;
    decomp_var_m8 = r30;
    r21 = self;
    r0 = [arg2 retain];
    r19 = r0;
    if (r0 != 0x0 && [r19 isKindOfClass:[MDVxDatabaseItem class]] != 0x0) {
            r20 = [r19 retain];
            r23 = [[UIDtoBentoObj alloc] initWithUID:[r20 DB_materialPackageUID] + 0x10];
            r22 = [[r21[0xa] TreefindNode:r23] retain];
            [r23 release];
            if (r22 != 0x0) {
                    r24 = [r21[0xe] TreefindNode:r22[0x4]];
            }
            else {
                    r24 = 0xffffffff;
            }
            r23 = [UIDtoBentoObj alloc];
            r23 = [r23 initWithUID:[r20 DB_filePackageUID] + 0x10];
            [r22 release];
            r22 = [[r21[0xb] TreefindNode:r23] retain];
            [r23 release];
            if (r22 != 0x0) {
                    r23 = [r21[0xe] TreefindNode:r22[0x4]];
            }
            else {
                    r23 = 0xffffffff;
            }
            if (r24 != -0x1) {
                    r25 = [[r21 GetStringForObjectAt:r24] retain];
                    [r20 setDB_NameInBin:r25];
                    [r25 release];
                    if ([r21 lookForProperty:0x1033d At:r24] != 0x0) {
                            r8 = r21[0x28];
                            r9 = r21[0x3];
                            r10 = 0x18;
                            asm { umaddl     x9, w0, w10, x9 };
                            r9 = r9[0x3];
                            asm { rev        w10, w9 };
                            if (r8 != 0x0) {
                                    r2 = r9;
                            }
                            else {
                                    r10 = 0x18;
                                    r2 = 0x18;
                            }
                            [r20 setDB_usageCode:r2];
                    }
                    r3 = r24;
                    r0 = [r21 lookForProperty:0x10293 At:r3];
                    if (r0 != 0x0) {
                            r2 = r0;
                            r8 = r21[0x3];
                            asm { umaddl     x8, w0, w9, x8 };
                            if (r8[0x4] >= 0x8) {
                                    r0 = [r21 pDataFor:r2];
                                    if (r0 != 0x0) {
                                            r8 = r21[0x28];
                                            r2 = *(int32_t *)r0;
                                            if (r8 != 0x0) {
                                                    r3 = r0[0x1];
                                            }
                                            else {
                                                    asm { rev        w2, w2 };
                                                    asm { rev        w3, w8 };
                                            }
                                            [r20 setFrameRateNumerator:r2 denominator:r3];
                                    }
                            }
                    }
                    r0 = [r21 lookForProperty:0x1028e At:r24];
                    if (r0 != 0x0) {
                            r0 = [r21 pDataFor:r0];
                            if (r0 != 0x0) {
                                    r0 = [r21[0xe] TreefindNode:*(int32_t *)r0];
                                    if (r0 != -0x1) {
                                            r0 = [r21 lookForProperty:0x1026b At:r0];
                                            r0 = [r21 pDataFor:r0];
                                            if (r0 != 0x0) {
                                                    r8 = *(int16_t *)r0;
                                                    r24 = r0 + 0x2;
                                                    r9 = r21[0x28];
                                                    asm { rev        w10, w8 };
                                                    r10 = r10 >> 0x10;
                                                    if (r9 == 0x0) {
                                                            r8 = r10;
                                                    }
                                                    if ((r8 & 0xffff) != 0x0) {
                                                            r25 = r8 & 0xffff;
                                                            do {
                                                                    r2 = *(int32_t *)r24;
                                                                    r0 = r21[0xe];
                                                                    r0 = [r0 TreefindNode:r2];
                                                                    if (r0 != -0x1) {
                                                                            r0 = [r21 lookForProperty:0x10257 At:r0];
                                                                            if (r0 != 0x0) {
                                                                                    r0 = [r21 pDataFor:r0];
                                                                                    if (r0 != 0x0) {
                                                                                            if ((*r0 ^ 0x5354524f504d495f | *(r0 + 0x6) ^ 0x474e495454455354) == 0x0) {
                                                                                                    [r20 setDB_usageCode:0xf];
                                                                                            }
                                                                                    }
                                                                            }
                                                                    }
                                                                    r24 = r24 + 0x8;
                                                                    r25 = r25 - 0x1;
                                                            } while (r25 != 0x0);
                                                    }
                                            }
                                    }
                            }
                    }
            }
            if (r23 != -0x1) {
                    r0 = [r21 getProjectNameForObjAt:r23];
                    r0 = [r0 retain];
                    r24 = r0;
                    if (r0 != 0x0) {
                            [r20 setDB_ProjectName:r24];
                    }
                    r0 = [r21 getCompression_data:&var_68 forObjAt:r23];
                    if (r0 != 0x0) {
                            *(int32_t *)[r20 DB_CompressionFourCC] = var_68;
                            [r20 setDB_DIDResolutionID:var_5C];
                            [r20 setSampleRateNumerator:var_64 denominator:var_64];
                            [r20 setDB_bits:var_58];
                    }
                    [r24 release];
            }
            [r22 release];
            [r20 release];
    }
    [r19 release];
    return;
