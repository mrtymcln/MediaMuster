    r31 = r31 - 0x90;
    var_70 = d11;
    decomp_var_68 = d10;
    var_60 = d9;
    decomp_var_58 = d8;
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
    r20 = arg0;
    r19 = r8;
    r21 = *arg0;
    r23 = SAR(r21 >> 0x10, 0x10);
    asm { ubfx       x22, x21, #0x20, #0x10 };
    r25 = r23 + 0x1;
    r24 = 0x1;
    d10 = 0x4024000000000000;
    while (true) {
            if (EqualExactEditRate(r24 * 0x5da8 | 0xfffd00000000, r21) != 0x0) {
                    r8 = 0x5dc0;
                    temp_1 = 0x1;
                    break;
            }
            d0 = 0x3ff0000000000000;
            if (r23 >= 0x1) {
                    r8 = r25;
                    do {
                            d0 = d0 * d10;
                            r8 = r8 - 0x1;
                    } while (r8 > 0x1);
            }
            else {
                    if ((r23 & 0xffffffff80000000) != 0x0) {
                            r8 = r23;
                            do {
                                    d0 = d0 / d10;
                                    r8 = r8 + 0x1;
                            } while (r8 < 0x0);
                    }
            }
            r24 = r24 + 0x1;
            asm { ccmp       w24, #0xb, #0x4, mi };
            if (asm { ccmp       w24, #0xb, #0x4, mi }) {
                    continue;
            }
            r25 = r23 + 0x1;
            r24 = 0x1;
            d10 = 0x4024000000000000;
            while (true) {
                    if (EqualExactEditRate(r24 * 0xbb5 | 0xfffe00000000, r21) != 0x0) {
                            r8 = 0x7530;
                            temp_0 = 0x1;
                            break;
                    }
                    d0 = 0x3ff0000000000000;
                    if (r23 >= 0x1) {
                            r8 = r25;
                            do {
                                    d0 = d0 * d10;
                                    r8 = r8 - 0x1;
                            } while (r8 > 0x1);
                    }
                    else {
                            if ((r23 & 0xffffffff80000000) != 0x0) {
                                    r8 = r23;
                                    do {
                                            d0 = d0 / d10;
                                            r8 = r8 + 0x1;
                                    } while (r8 < 0x0);
                            }
                    }
                    r24 = r24 + 0x1;
                    asm { ccmp       w24, #0xb, #0x4, mi };
                    if (asm { ccmp       w24, #0xb, #0x4, mi }) {
                            continue;
                    }
                    if (sign_extend_64(r22) >= 0x1) {
                            r8 = r21 >> 0x20;
                            asm { ubfx       x9, x21, #0x20, #0xf };
                            r8 = r8 + 0x3 & 0xfffc;
                            asm { dup        v0.4s, w9 };
                            v1 = 0x1;
                            r9 = 0x4;
                            v3 = 0xa;
                            temp_0 = 0x0;
                    }
                    else {
                            if (r22 != 0x0) {
                                    r8 = (r22 ^ 0xffff) + 0x4 & 0x1fffc;
                                    asm { dup        v0.4s, w9 };
                                    v1 = 0x1;
                                    r9 = 0x4;
                                    v3 = 0xa;
                                    temp_0 = 0x2;
                            }
                            else {
                                    r8 = 0x1;
                                    temp_0 = 0x3;
                            }
                    }
                    break;
            }
            switch (temp_0) {
                case 0:
                    temp_1 = 0x0;
                    break;
                case 1:
                    temp_1 = 0x1;
                    break;
                case 2:
                    temp_1 = 0x2;
                    break;
                default:
                    temp_1 = 0x3;
                    break;
            }
            break;
    }
    switch (temp_1) {
        case 0:
            do {
                    v1 = v1 * v3;
                    r9 = r9 - 0x4;
            } while (r8 + r9 != 0x4);
            asm { dup        v3.4s, w8 };
            asm { cmhi       v0.4s, v3.4s, v0.4s };
            asm { bsl        v0.16b, v2.16b, v1.16b };
            asm { ext        v1.16b, v0.16b, v0.16b, #0x8 };
            var_74 = s0 * v0 * v1 * r21;
            r1 = &var_74;
            r0 = r19;
            break;
        case 1:
            var_74 = r24 * r8;
            r1 = &var_74;
            r0 = r19;
            break;
        default:
            if (temp_1 == 0x2) {
                    do {
                            v1 = v1 * v3;
                            r9 = r9 - 0x4;
                    } while (r8 + r9 != 0x4);
                    asm { dup        v3.4s, w8 };
                    asm { cmhi       v0.4s, v3.4s, v0.4s };
                    asm { bsl        v0.16b, v2.16b, v1.16b };
                    asm { ext        v1.16b, v0.16b, v0.16b, #0x8 };
                    r8 = s0 * v0 * v1;
            }
            r0 = r19;
            r1 = r20;
            break;
    }
    r0 = Rational::Rational(r0, r1);
    return r0;
