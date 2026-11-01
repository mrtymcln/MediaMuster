    r2 = arg2;
    r0 = self;
    var_20 = r22;
    decomp_var_18 = r21;
    r31 = r31 + 0xffffffffffffffd0;
    var_10 = r20;
    decomp_var_8 = r19;
    saved_fp = r29;
    decomp_var_m8 = r30;
    if (r2 != 0x0) {
            r19 = r0;
            r14 = r0[0x3];
            r8 = *(int32_t *)(r14 + r2 * 0x18);
            r21 = r2 + r2 * 0x2 << 0x3;
            while (true) {
                    r14 = r14 + r21;
                    asm { ccmp       x16, #0x0, #0x4, ls };
                    asm { ccmp       w15, w12, #0x4, eq };
                    asm { ccmp       w15, w13, #0x4, ne };
                    if (asm { ccmp       w15, w13, #0x4, ne }) {
                            r0 = [r19 pDataFor:r2];
                            if (r0 != 0x0) {
                                    if (*(int32_t *)(r19[0x3] + r21 + 0x10) != 0x0) {
                                            r20 = r0;
                                            r22 = r19[0x28];
                                            r0 = [NSString alloc];
                                            r3 = *(int32_t *)(r19[0x3] + r21 + 0x10);
                                            if (r22 != 0x0) {
                                                    r4 = 0x1;
                                                    temp_0 = 0x0;
                                            }
                                            else {
                                                    r4 = 0x1e;
                                                    temp_0 = 0x0;
                                            }
                                    }
                                    else {
                                            temp_0 = 0x3;
                                    }
                            }
                            else {
                                    temp_0 = 0x2;
                            }
                            break;
                    }
                    if (r14[0x6] == r8) {
                            r14 = r19[0x3];
                            r2 = r2 + 0x1;
                            r21 = r21 + 0x18;
                            continue;
                    }
                    temp_0 = 0x1;
                    break;
            }
            if (temp_0 != 0x0) {
                    r19 = 0x0;
            }
            else {
                    r0 = [r0 initWithBytes:r20 length:r3 encoding:r4];
                    if (r0 != 0x0) {
                            r21 = [[NSCharacterSet characterSetWithCharactersInString:@""] retain];
                            r19 = [[r0 stringByTrimmingCharactersInSet:r21] retain];
                            [r0 release];
                            [r21 release];
                    }
                    else {
                            r19 = 0x0;
                    }
            }
    }
    else {
            r19 = 0x0;
    }
    r0 = [r19 autorelease];
    return r0;
