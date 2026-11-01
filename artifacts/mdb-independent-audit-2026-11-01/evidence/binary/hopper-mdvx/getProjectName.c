    r2 = arg2;
    r1 = _cmd;
    r0 = self;
    r31 = r31 - 0xa0;
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
    if (r2 != -0x1) {
            r22 = r2;
            r25 = r1;
            r20 = r0;
            r0 = [r0 getProjectAttributeForObjectAt:r2];
            r0 = [r0 retain];
            if (r0 != 0x0) {
                    r21 = r0;
                    var_58 = 0x0;
            }
            else {
                    r0 = [r20 lookForProperty:0x1040f At:r22];
                    if (r0 != 0x0) {
                            r0 = [r20 pDataFor:r0];
                            if (r0 != 0x0) {
                                    r8 = *(int16_t *)r0;
                                    r19 = r0 + 0x2;
                                    r9 = r20[0x28];
                                    asm { rev        w10, w8 };
                                    r10 = r10 >> 0x10;
                                    if (r9 == 0x0) {
                                            r8 = r10;
                                    }
                                    if ((r8 & 0xffff) != 0x0) {
                                            r22 = 0x0;
                                            r21 = 0x0;
                                            var_60 = r19;
                                            var_58 = 0x0;
                                            r28 = r8 & 0xffff;
                                            var_78 = r25;
                                            var_68 = r28;
                                            do {
                                                    r0 = r20[0xe];
                                                    r0 = [r0 TreefindNode:*(int32_t *)(r19 + (r22 << 0x3))];
                                                    if (r0 == -0x1) {
                                                            r22 = r22 + 0x1;
                                                            if (r22 != r28) {
                                                                    continue;
                                                            }
                                                            break;
                                                    }
                                                    r0 = [r20 lookForProperty:0x103f6 At:r0];
                                                    if (r0 == 0x0) {
                                                            r22 = r22 + 0x1;
                                                            if (r22 != r28) {
                                                                    continue;
                                                            }
                                                            break;
                                                    }
                                                    r0 = [r20 pDataFor:r0];
                                                    if (r0 == 0x0) {
                                                            r22 = r22 + 0x1;
                                                            if (r22 != r28) {
                                                                    continue;
                                                            }
                                                            break;
                                                    }
                                                    r0 = [r20[0xe] TreefindNode:*(int32_t *)r0];
                                                    if (r0 != -0x1) {
                                                            r0 = [r20 lookForProperty:0x10374 At:r0];
                                                            if (r0 != 0x0) {
                                                                    r0 = [r20 pDataFor:r0];
                                                                    if (r0 != 0x0) {
                                                                            r8 = *(int16_t *)r0;
                                                                            r24 = r0 + 0x2;
                                                                            r9 = r20[0x28];
                                                                            asm { rev        w10, w8 };
                                                                            r10 = r10 >> 0x10;
                                                                            if (r9 == 0x0) {
                                                                                    r8 = r10;
                                                                            }
                                                                            if ((r8 & 0xffff) != 0x0) {
                                                                                    r23 = r8 & 0xffff;
                                                                                    while (true) {
                                                                                            r0 = r20[0xe];
                                                                                            r0 = [r0 TreefindNode:*(int32_t *)r24];
                                                                                            if (r0 == -0x1) {
                                                                                                    r24 = r24 + 0x8;
                                                                                                    r23 = r23 - 0x1;
                                                                                                    if (r23 != 0x0) {
                                                                                                            continue;
                                                                                                    }
                                                                                                    break;
                                                                                            }
                                                                                            r3 = r0;
                                                                                            r0 = [r20 lookForProperty:0x10356 At:r3];
                                                                                            if (r0 == 0x0) {
                                                                                                    r24 = r24 + 0x8;
                                                                                                    r23 = r23 - 0x1;
                                                                                                    if (r23 != 0x0) {
                                                                                                            continue;
                                                                                                    }
                                                                                                    break;
                                                                                            }
                                                                                            r27 = r0;
                                                                                            r8 = r20[0x3];
                                                                                            asm { umaddl     x8, w0, w9, x8 };
                                                                                            r19 = r8[0x4];
                                                                                            r0 = [r20 pDataFor:r27];
                                                                                            if (r0 == 0x0) {
                                                                                                    r28 = var_68;
                                                                                                    r19 = var_60;
                                                                                                    r24 = r24 + 0x8;
                                                                                                    r23 = r23 - 0x1;
                                                                                                    if (r23 != 0x0) {
                                                                                                            continue;
                                                                                                    }
                                                                                                    break;
                                                                                            }
                                                                                            r28 = r0;
                                                                                            if (r19 != 0x20) {
                                                                                                    if (r19 == 0xc) {
                                                                                                            r0 = [UIDtoBentoObj alloc];
                                                                                                            r2 = r28 + 0x4;
                                                                                                            r27 = [r0 initWithUID:r2];
                                                                                                            [var_58 release];
                                                                                                            r19 = [[r20[0xb] TreefindNode:r27] retain];
                                                                                                            [r27 release];
                                                                                                            if (r19 != 0x0) {
                                                                                                                    var_58 = r19;
                                                                                                                    r27 = r21;
                                                                                                                    r21 = [[r20 getProjectAttributeForObjectAt:[r20[0xe] TreefindNode:r19[0x4]]] retain];
                                                                                                                    r28 = var_68;
                                                                                                                    r19 = var_60;
                                                                                                                    [r27 release];
                                                                                                                    r24 = r24 + 0x8;
                                                                                                                    r23 = r23 - 0x1;
                                                                                                                    if (r23 != 0x0) {
                                                                                                                            continue;
                                                                                                                    }
                                                                                                                    break;
                                                                                                            }
                                                                                                            else {
                                                                                                                    var_58 = 0x0;
                                                                                                                    r28 = var_68;
                                                                                                                    r19 = var_60;
                                                                                                                    r24 = r24 + 0x8;
                                                                                                                    r23 = r23 - 0x1;
                                                                                                                    if (r23 != 0x0) {
                                                                                                                            continue;
                                                                                                                    }
                                                                                                                    break;
                                                                                                            }
                                                                                                    }
                                                                                                    else {
                                                                                                            r25 = [[NSString stringWithFormat:@"ERROR IN OMFI PARSER. UID capacity is::%ul\n", r3] retain];
                                                                                                            [xConsole xNSLog:"/Volumes/MyHome/Programming/Xcode_projects/MDVx/MDVx1/MDVxCore/MDVxOMFI.m" :0x614 :"-[MDVxOMFI getProjectNameForObjAt:]" :r25];
                                                                                                            [r25 release];
                                                                                                            r26 = [NSStringFromClass([r20 class]) retain];
                                                                                                            r19 = [NSStringFromSelector(var_78) retain];
                                                                                                            r28 = [[NSString stringWithFormat:@"[%@ %@]", r3, r4] retain];
                                                                                                            [xConsole xNSLog:"/Volumes/MyHome/Programming/Xcode_projects/MDVx/MDVx1/MDVxCore/MDVxOMFI.m" :0x615 :"-[MDVxOMFI getProjectNameForObjAt:]" :r28];
                                                                                                            [r28 release];
                                                                                                            [r19 release];
                                                                                                            [r26 release];
                                                                                                            r27 = [[r20 StringForTocIndexItem:r27] retain];
                                                                                                            r25 = [[NSString stringWithFormat:@"%@\n", r3] retain];
                                                                                                            [xConsole xNSLog:"/Volumes/MyHome/Programming/Xcode_projects/MDVx/MDVx1/MDVxCore/MDVxOMFI.m" :0x616 :"-[MDVxOMFI getProjectNameForObjAt:]" :r25];
                                                                                                            [r25 release];
                                                                                                            r4 = "-[MDVxOMFI getProjectNameForObjAt:]";
                                                                                                            r28 = var_68;
                                                                                                            r19 = var_60;
                                                                                                            [r27 release];
                                                                                                            r24 = r24 + 0x8;
                                                                                                            r23 = r23 - 0x1;
                                                                                                            if (r23 != 0x0) {
                                                                                                                    continue;
                                                                                                            }
                                                                                                            break;
                                                                                                    }
                                                                                            }
                                                                                            else {
                                                                                                    r0 = [UIDtoBentoObj alloc];
                                                                                                    r2 = r28 + 0x10;
                                                                                                    r27 = [r0 initWithUID:r2];
                                                                                                    [var_58 release];
                                                                                                    r19 = [[r20[0xb] TreefindNode:r27] retain];
                                                                                                    [r27 release];
                                                                                                    if (r19 != 0x0) {
                                                                                                            var_58 = r19;
                                                                                                            r27 = r21;
                                                                                                            r21 = [[r20 getProjectAttributeForObjectAt:[r20[0xe] TreefindNode:r19[0x4]]] retain];
                                                                                                            r28 = var_68;
                                                                                                            r19 = var_60;
                                                                                                            [r27 release];
                                                                                                            r24 = r24 + 0x8;
                                                                                                            r23 = r23 - 0x1;
                                                                                                            if (r23 != 0x0) {
                                                                                                                    continue;
                                                                                                            }
                                                                                                            break;
                                                                                                    }
                                                                                                    else {
                                                                                                            var_58 = 0x0;
                                                                                                            r28 = var_68;
                                                                                                            r19 = var_60;
                                                                                                            r24 = r24 + 0x8;
                                                                                                            r23 = r23 - 0x1;
                                                                                                            if (r23 != 0x0) {
                                                                                                                    continue;
                                                                                                            }
                                                                                                            break;
                                                                                                    }
                                                                                            }
                                                                                    }
                                                                            }
                                                                    }
                                                            }
                                                    }
                                                    r22 = r22 + 0x1;
                                            } while (r22 != r28);
                                    }
                                    else {
                                            var_58 = 0x0;
                                            r21 = 0x0;
                                    }
                            }
                            else {
                                    var_58 = 0x0;
                                    r21 = 0x0;
                            }
                    }
                    else {
                            var_58 = 0x0;
                            r21 = 0x0;
                    }
            }
            [var_58 release];
    }
    else {
            r21 = 0x0;
    }
    r0 = [r21 autorelease];
    return r0;
