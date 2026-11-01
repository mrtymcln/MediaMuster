    r2 = arg2;
    r0 = self;
    var_20 = r22;
    decomp_var_18 = r21;
    r31 = r31 + 0xffffffffffffffd0;
    var_10 = r20;
    decomp_var_8 = r19;
    saved_fp = r29;
    decomp_var_m8 = r30;
    if (r2 != -0x1) {
            r19 = r0;
            r9 = r0[0x6];
            if (r9 != 0x0) {
                    r8 = r19[0x3];
                    if (r8 != 0x0) {
                            if (r9 >= zero_extend_64(r2)) {
                                    if (r19[0x2] != 0x0) {
                                            r20 = r2;
                                            r21 = *(int32_t *)(0x18 * r20 + 0x28);
                                            if (r21 != 0x0) {
                                                    if (*(int32_t *)(0x18 * r20 + 0x2c) == 0x1) {
                                                            r0 = r19[0x1];
                                                            r0 = [r0 length];
                                                            r8 = r19[0x3];
                                                            r8 = *(int32_t *)(r8 * r20 + 0x24);
                                                            if (r0 < r8 + r21) {
                                                                    r0 = 0x0;
                                                            }
                                                            else {
                                                                    r0 = r19[0x2] + r8;
                                                            }
                                                    }
                                                    else {
                                                            r0 = r8 * r20 + 0x24;
                                                    }
                                            }
                                            else {
                                                    r0 = 0x0;
                                            }
                                    }
                                    else {
                                            r0 = 0x0;
                                    }
                            }
                            else {
                                    r0 = 0x0;
                            }
                    }
                    else {
                            r0 = 0x0;
                    }
            }
            else {
                    r0 = 0x0;
            }
    }
    else {
            r0 = 0x0;
    }
    return r0;
