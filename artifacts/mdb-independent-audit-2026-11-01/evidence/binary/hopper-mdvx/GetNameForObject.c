    r31 = r31 - 0x70;
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
    r19 = arg2;
    r20 = self;
    r25 = *(int32_t *)(self[0x3] + arg2 * 0x18);
    if ([self pDataFor:arg2] == 0x0) goto loc_100063814;
    goto loc_1000637f0;

loc_100063814:
    r22 = 0x0;
    r21 = 0x0;
    r19 = 0x0;
    goto loc_100063820;

loc_1000637f0:
    r0 = r20[0xe];
    r0 = [r0 TreefindNode:*(int32_t *)(r20[0x3] + r19 * 0x18)];
    if (r0 >= 0x1) goto loc_100063854;
    goto loc_100063814;

loc_100063820:
    [r21 release];
    [r19 release];
    r0 = r22;
    return r0;

loc_100063854:
    r8 = r20[0x3];
    r9 = *(int32_t *)(r8 + r0 * 0x18);
    if (r9 != r25) goto loc_100063814;
    goto loc_100063868;

loc_100063868:
    r19 = 0x0;
    r21 = 0x0;
    goto loc_10006388c;

loc_10006388c:
    r28 = 0x102e0;
    r22 = r0;
    asm { umaddl     x9, w0, w27, x8 };
    r9 = r9[0x1];
    if (r9 <= r28) {
            asm { ccmp       w10, #0x0, #0x4, ls };
            asm { ccmp       w9, w24, #0x4, eq };
            if (!asm { ccmp       w9, w24, #0x4, eq }) {
                    r0 = r22 + 0x1;
                    r9 = r0 * 0x18;
                    if (*(int32_t *)(r8 + r9) == r25) {
                            r8 = r20[0x3];
                            continue;
                    }
                    temp_0 = 0x0;
                    break;
            }
            else {
                    r0 = [r20 pDataFor:r22];
                    if (r0 != 0x0 && *(int32_t *)(r20[0x3] * r22 + 0x28) != 0x0) {
                            r23 = r0;
                            if (r19 == 0x0) {
                                    r19 = [[NSMutableString alloc] init];
                            }
                            r26 = r20[0x28];
                            r0 = [NSString alloc];
                            r3 = *(int32_t *)(r20[0x3] * r22 + 0x28);
                            if (r26 != 0x0) {
                                    r4 = 0x1;
                            }
                            else {
                                    r4 = 0x1e;
                            }
                            r23 = [r0 initWithBytes:r23 length:r3 encoding:r4];
                            [r21 release];
                            if (r23 != 0x0) {
                                    [r19 appendString:r23];
                                    r21 = r23;
                            }
                            else {
                                    r21 = 0x0;
                            }
                    }
                    r0 = r22 + 0x1;
                    r8 = r20[0x3];
                    r9 = r0 * 0x18;
                    if (*(int32_t *)(r8 + r9) == r25) {
                            r8 = r20[0x3];
                            continue;
                    }
                    temp_0 = 0x0;
                    break;
            }
    }
    else {
            if (r9 == 0x102e1) {
                    r0 = [r20 pDataFor:r22];
                    if (r0 != 0x0 && *(int32_t *)(r20[0x3] * r22 + 0x28) != 0x0) {
                            r23 = r0;
                            if (r19 == 0x0) {
                                    r19 = [[NSMutableString alloc] init];
                            }
                            r26 = r20[0x28];
                            r0 = [NSString alloc];
                            r3 = *(int32_t *)(r20[0x3] * r22 + 0x28);
                            if (r26 != 0x0) {
                                    r4 = 0x1;
                            }
                            else {
                                    r4 = 0x1e;
                            }
                            r23 = [r0 initWithBytes:r23 length:r3 encoding:r4];
                            [r21 release];
                            if (r23 != 0x0) {
                                    [r19 appendString:r23];
                                    r21 = r23;
                            }
                            else {
                                    r21 = 0x0;
                            }
                    }
                    r0 = r22 + 0x1;
                    r8 = r20[0x3];
                    r9 = r0 * 0x18;
                    if (*(int32_t *)(r8 + r9) == r25) {
                            r8 = r20[0x3];
                            continue;
                    }
                    temp_0 = 0x0;
                    break;
            }
            else {
                    if (r9 != 0x1032e) {
                            r0 = r22 + 0x1;
                            r9 = r0 * 0x18;
                            if (*(int32_t *)(r8 + r9) == r25) {
                                    r8 = r20[0x3];
                                    continue;
                            }
                            temp_0 = 0x0;
                            break;
                    }
                    else {
                            r20 = [[r20 getStringForUID:r22] retain];
                            [r21 release];
                            if (r19 != 0x0) {
                                    if (r20 != 0x0) {
                                            [r19 appendFormat:@"\" \"UID:%@", r3];
                                            temp_0 = 0x3;
                                    }
                                    else {
                                            temp_0 = 0x1;
                                    }
                            }
                            else {
                                    r19 = [[NSMutableString alloc] init];
                                    if (r20 != 0x0) {
                                            [r19 appendString:r20];
                                            r21 = r20;
                                            temp_0 = 0x0;
                                    }
                                    else {
                                            r21 = 0x0;
                                            temp_0 = 0x2;
                                    }
                            }
                            break;
                    }
            }
    }
    goto .l1;

.l1:
    switch (temp_0) {
        case 0:
            if (r19 != 0x0) {
                    r20 = r21;
                    r22 = [[[NSString stringWithString:r19] retain] autorelease];
                    r21 = r20;
            }
            else {
                    r22 = 0x0;
            }
            break;
        case 1:
            r22 = [[[NSString stringWithString:r19] retain] autorelease];
            r21 = r20;
            break;
        case 2:
            if (r19 != 0x0) {
                    r20 = r21;
                    r22 = [[[NSString stringWithString:r19] retain] autorelease];
                    r21 = r20;
            }
            else {
                    r22 = 0x0;
            }
            break;
        default:
            r22 = [[[NSString stringWithString:r19] retain] autorelease];
            r21 = r20;
            break;
    }
    goto loc_100063820;
