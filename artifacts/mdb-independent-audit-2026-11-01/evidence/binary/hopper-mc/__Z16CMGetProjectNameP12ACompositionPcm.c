    r2 = arg2;
    r1 = arg1;
    r0 = arg0;
    r31 = r31 - 0x40;
    var_20 = r22;
    decomp_var_18 = r21;
    var_10 = r20;
    decomp_var_8 = r19;
    saved_fp = r29;
    decomp_var_m8 = r30;
    if (r0 != 0x0) {
            r20 = r2;
            r19 = r1;
            r0 = (*(*r0 + 0x250))();
            if (r0 != 0x0) {
                    r21 = r0;
                    *(int8_t *)r19 = 0x0;
                    (*(*r0 + 0x1e8))();
                    if (var_21 == 0x0) {
                            (*(*r21 + 0x1e8))(r21, "PROJNAME", r19, r20, &var_21);
                            if (var_21 == 0x0) {
                                    r19 = 0x0;
                            }
                    }
            }
            else {
                    r19 = 0x0;
            }
    }
    else {
            r19 = 0x0;
    }
    r0 = r19;
    return r0;
