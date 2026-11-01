    r31 = r31 - 0x120;
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
    r20 = arg1;
    r21 = arg0;
    var_58 = **___stack_chk_guard;
    r22 = CompIsPendingFMob(arg1);
    r0 = CompIsPendingFMob(r21);
    if (r22 != 0x0) {
            if (r0 == 0x0) {
                    MigrateSomePendingInfoToFinalMob(r20, r21);
                    r0 = 0x2;
                    if (**___stack_chk_guard != var_58) {
                            r0 = 0x2;
                            __stack_chk_fail();
                    }
            }
            else {
                    if (r0 != 0x0) {
                            if (r22 == 0x0) {
                                    MigrateSomePendingInfoToFinalMob(r21, r20);
                                    if (r19 != 0x0) {
                                            AComposition::GetMobID();
                                            (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                    }
                                    r0 = 0x3;
                                    if (**___stack_chk_guard != var_58) {
                                            r0 = 0x3;
                                            __stack_chk_fail();
                                    }
                            }
                            else {
                                    r22 = (*(*r21 + "/../Frameworks/AvidCoreServices.framework/Versions/A/AvidCoreServices"))(r21);
                                    r23 = (*(*r20 + "/../Frameworks/AvidCoreServices.framework/Versions/A/AvidCoreServices"))(r20);
                                    r24 = CMGetGeneration(r21);
                                    r25 = CMGetGeneration(r20);
                                    if ((*(*r20 + "tils.framework/Versions/A/AvidCoreUtils"))(r20) == 0x3 && AComposition::GetMediaDesc() != 0x0) {
                                            if (AMediaDesc::GetPhysMobType() == 0x2) {
                                                    r26 = 0x1;
                                            }
                                    }
                                    else {
                                            r26 = 0x0;
                                    }
                                    if (r25 <= r24) {
                                            r8 = *r20;
                                            r8 = r8["tils.framework/Versions/A/AvidCoreUtils"];
                                            if ((r8)(r20) != 0x2) {
                                                    r8 = 0x1;
                                            }
                                            if ((r8 & !r26 & 0x1) == 0x0) {
                                                    XError::XError();
                                                    if ((r22 | r23) == 0x0 && (*(*r20 + 0x1f8))(r20) > (*(*r21 + 0x1f8))(r21)) {
                                                            if ((*(*r20 + 0xae8))(r20) == 0x0) {
                                                                    if ((*(*r21 + 0xae8))(r21) != 0x0) {
                                                                            XError::XError();
                                                                            r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                                            XError::~XError();
                                                                            r22 = ResolveMerge(r20, r23, 0x1);
                                                                    }
                                                                    else {
                                                                            r22 = ResolveMerge(r21, r20, 0x0);
                                                                            r23 = 0x0;
                                                                    }
                                                            }
                                                            else {
                                                                    XError::XError();
                                                                    r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                                    XError::~XError();
                                                                    r22 = ResolveMerge(r20, r23, 0x1);
                                                            }
                                                    }
                                                    else {
                                                            XError::XError();
                                                            r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                            XError::~XError();
                                                            r22 = ResolveMerge(r20, r23, 0x1);
                                                    }
                                                    r0 = StSetCurrentException::StSetCurrentException(&decomp_var_c8);
                                                    if (r22 != 0x0) {
                                                            if (var_90 != 0x0) {
                                                                    if (r23 != 0x0) {
                                                                            (**r23)(r23);
                                                                            r23 = 0x0;
                                                                    }
                                                            }
                                                    }
                                                    else {
                                                            if (r23 != 0x0) {
                                                                    (**r23)(r23);
                                                                    r23 = 0x0;
                                                            }
                                                    }
                                                    if (var_90 != 0x0) {
                                                            r0 = xraisexerror(&decomp_var_a0);
                                                    }
                                                    StSetCurrentException::~StSetCurrentException();
                                                    XError::~XError();
                                                    if (r22 != 0x0) {
                                                            (*(*GetMobManProtocol() + 0xe0))();
                                                            if (r19 != 0x0) {
                                                                    AComposition::GetMobID();
                                                                    (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                                            }
                                                            r0 = 0x3;
                                                            if (**___stack_chk_guard != var_58) {
                                                                    r0 = 0x3;
                                                                    __stack_chk_fail();
                                                            }
                                                    }
                                                    else {
                                                            if ((*(*r20 + "tils.framework/Versions/A/AvidCoreUtils"))(r20) == 0x3) {
                                                                    if (AComposition::GetMediaDesc() != 0x0) {
                                                                            r0 = __dynamic_cast();
                                                                            if (r0 != 0x0) {
                                                                                    if ((*(*r0 + 0x2a0))() >= 0x1) {
                                                                                            if (AComposition::GetMediaDesc() != 0x0) {
                                                                                                    r0 = __dynamic_cast();
                                                                                                    if (r0 != 0x0) {
                                                                                                            if ((*(*r0 + 0x2a0))() == 0x0) {
                                                                                                                    if (r19 != 0x0) {
                                                                                                                            AComposition::GetMobID();
                                                                                                                            (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                                                                                                    }
                                                                                                                    r0 = 0x3;
                                                                                                            }
                                                                                                            else {
                                                                                                                    r0 = 0x2;
                                                                                                            }
                                                                                                    }
                                                                                                    else {
                                                                                                            r0 = 0x2;
                                                                                                    }
                                                                                            }
                                                                                            else {
                                                                                                    r0 = 0x2;
                                                                                            }
                                                                                    }
                                                                                    else {
                                                                                            r0 = 0x2;
                                                                                    }
                                                                            }
                                                                            else {
                                                                                    r0 = 0x2;
                                                                            }
                                                                    }
                                                                    else {
                                                                            r0 = 0x2;
                                                                    }
                                                            }
                                                            else {
                                                                    r0 = 0x2;
                                                            }
                                                            if (**___stack_chk_guard != var_58) {
                                                                    __stack_chk_fail();
                                                            }
                                                    }
                                            }
                                            else {
                                                    if ((*(*r20 + "tils.framework/Versions/A/AvidCoreUtils"))(r20) == 0x3) {
                                                            if (AComposition::GetMediaDesc() != 0x0) {
                                                                    r0 = __dynamic_cast();
                                                                    if (r0 != 0x0) {
                                                                            if ((*(*r0 + 0x2a0))() >= 0x1) {
                                                                                    if (AComposition::GetMediaDesc() != 0x0) {
                                                                                            r0 = __dynamic_cast();
                                                                                            if (r0 != 0x0) {
                                                                                                    if ((*(*r0 + 0x2a0))() == 0x0) {
                                                                                                            if (r19 != 0x0) {
                                                                                                                    AComposition::GetMobID();
                                                                                                                    (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                                                                                            }
                                                                                                            r0 = 0x3;
                                                                                                    }
                                                                                                    else {
                                                                                                            r0 = 0x2;
                                                                                                    }
                                                                                            }
                                                                                            else {
                                                                                                    r0 = 0x2;
                                                                                            }
                                                                                    }
                                                                                    else {
                                                                                            r0 = 0x2;
                                                                                    }
                                                                            }
                                                                            else {
                                                                                    r0 = 0x2;
                                                                            }
                                                                    }
                                                                    else {
                                                                            r0 = 0x2;
                                                                    }
                                                            }
                                                            else {
                                                                    r0 = 0x2;
                                                            }
                                                    }
                                                    else {
                                                            r0 = 0x2;
                                                    }
                                                    if (**___stack_chk_guard != var_58) {
                                                            __stack_chk_fail();
                                                    }
                                            }
                                    }
                                    else {
                                            XError::XError();
                                            if ((r22 | r23) == 0x0 && (*(*r20 + 0x1f8))(r20) > (*(*r21 + 0x1f8))(r21)) {
                                                    if ((*(*r20 + 0xae8))(r20) == 0x0) {
                                                            if ((*(*r21 + 0xae8))(r21) != 0x0) {
                                                                    XError::XError();
                                                                    r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                                    XError::~XError();
                                                                    r22 = ResolveMerge(r20, r23, 0x1);
                                                            }
                                                            else {
                                                                    r22 = ResolveMerge(r21, r20, 0x0);
                                                                    r23 = 0x0;
                                                            }
                                                    }
                                                    else {
                                                            XError::XError();
                                                            r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                            XError::~XError();
                                                            r22 = ResolveMerge(r20, r23, 0x1);
                                                    }
                                            }
                                            else {
                                                    XError::XError();
                                                    r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                    XError::~XError();
                                                    r22 = ResolveMerge(r20, r23, 0x1);
                                            }
                                            r0 = StSetCurrentException::StSetCurrentException(&decomp_var_c8);
                                            if (r22 != 0x0) {
                                                    if (var_90 != 0x0) {
                                                            if (r23 != 0x0) {
                                                                    (**r23)(r23);
                                                                    r23 = 0x0;
                                                            }
                                                    }
                                            }
                                            else {
                                                    if (r23 != 0x0) {
                                                            (**r23)(r23);
                                                            r23 = 0x0;
                                                    }
                                            }
                                            if (var_90 != 0x0) {
                                                    r0 = xraisexerror(&decomp_var_a0);
                                            }
                                            StSetCurrentException::~StSetCurrentException();
                                            XError::~XError();
                                            if (r22 != 0x0) {
                                                    (*(*GetMobManProtocol() + 0xe0))();
                                                    if (r19 != 0x0) {
                                                            AComposition::GetMobID();
                                                            (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                                    }
                                                    r0 = 0x3;
                                                    if (**___stack_chk_guard != var_58) {
                                                            r0 = 0x3;
                                                            __stack_chk_fail();
                                                    }
                                            }
                                            else {
                                                    if ((*(*r20 + "tils.framework/Versions/A/AvidCoreUtils"))(r20) == 0x3) {
                                                            if (AComposition::GetMediaDesc() != 0x0) {
                                                                    r0 = __dynamic_cast();
                                                                    if (r0 != 0x0) {
                                                                            if ((*(*r0 + 0x2a0))() >= 0x1) {
                                                                                    if (AComposition::GetMediaDesc() != 0x0) {
                                                                                            r0 = __dynamic_cast();
                                                                                            if (r0 != 0x0) {
                                                                                                    if ((*(*r0 + 0x2a0))() == 0x0) {
                                                                                                            if (r19 != 0x0) {
                                                                                                                    AComposition::GetMobID();
                                                                                                                    (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                                                                                            }
                                                                                                            r0 = 0x3;
                                                                                                    }
                                                                                                    else {
                                                                                                            r0 = 0x2;
                                                                                                    }
                                                                                            }
                                                                                            else {
                                                                                                    r0 = 0x2;
                                                                                            }
                                                                                    }
                                                                                    else {
                                                                                            r0 = 0x2;
                                                                                    }
                                                                            }
                                                                            else {
                                                                                    r0 = 0x2;
                                                                            }
                                                                    }
                                                                    else {
                                                                            r0 = 0x2;
                                                                    }
                                                            }
                                                            else {
                                                                    r0 = 0x2;
                                                            }
                                                    }
                                                    else {
                                                            r0 = 0x2;
                                                    }
                                                    if (**___stack_chk_guard != var_58) {
                                                            __stack_chk_fail();
                                                    }
                                            }
                                    }
                            }
                    }
                    else {
                            r22 = (*(*r21 + "/../Frameworks/AvidCoreServices.framework/Versions/A/AvidCoreServices"))(r21);
                            r23 = (*(*r20 + "/../Frameworks/AvidCoreServices.framework/Versions/A/AvidCoreServices"))(r20);
                            r24 = CMGetGeneration(r21);
                            r25 = CMGetGeneration(r20);
                            if ((*(*r20 + "tils.framework/Versions/A/AvidCoreUtils"))(r20) == 0x3 && AComposition::GetMediaDesc() != 0x0) {
                                    if (AMediaDesc::GetPhysMobType() == 0x2) {
                                            r26 = 0x1;
                                    }
                            }
                            else {
                                    r26 = 0x0;
                            }
                            if (r25 <= r24) {
                                    r8 = *r20;
                                    r8 = r8["tils.framework/Versions/A/AvidCoreUtils"];
                                    if ((r8)(r20) != 0x2) {
                                            r8 = 0x1;
                                    }
                                    if ((r8 & !r26 & 0x1) == 0x0) {
                                            XError::XError();
                                            if ((r22 | r23) == 0x0 && (*(*r20 + 0x1f8))(r20) > (*(*r21 + 0x1f8))(r21)) {
                                                    if ((*(*r20 + 0xae8))(r20) == 0x0) {
                                                            if ((*(*r21 + 0xae8))(r21) != 0x0) {
                                                                    XError::XError();
                                                                    r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                                    XError::~XError();
                                                                    r22 = ResolveMerge(r20, r23, 0x1);
                                                            }
                                                            else {
                                                                    r22 = ResolveMerge(r21, r20, 0x0);
                                                                    r23 = 0x0;
                                                            }
                                                    }
                                                    else {
                                                            XError::XError();
                                                            r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                            XError::~XError();
                                                            r22 = ResolveMerge(r20, r23, 0x1);
                                                    }
                                            }
                                            else {
                                                    XError::XError();
                                                    r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                    XError::~XError();
                                                    r22 = ResolveMerge(r20, r23, 0x1);
                                            }
                                            r0 = StSetCurrentException::StSetCurrentException(&decomp_var_c8);
                                            if (r22 != 0x0) {
                                                    if (var_90 != 0x0) {
                                                            if (r23 != 0x0) {
                                                                    (**r23)(r23);
                                                                    r23 = 0x0;
                                                            }
                                                    }
                                            }
                                            else {
                                                    if (r23 != 0x0) {
                                                            (**r23)(r23);
                                                            r23 = 0x0;
                                                    }
                                            }
                                            if (var_90 != 0x0) {
                                                    r0 = xraisexerror(&decomp_var_a0);
                                            }
                                            StSetCurrentException::~StSetCurrentException();
                                            XError::~XError();
                                            if (r22 != 0x0) {
                                                    (*(*GetMobManProtocol() + 0xe0))();
                                                    if (r19 != 0x0) {
                                                            AComposition::GetMobID();
                                                            (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                                    }
                                                    r0 = 0x3;
                                                    if (**___stack_chk_guard != var_58) {
                                                            r0 = 0x3;
                                                            __stack_chk_fail();
                                                    }
                                            }
                                            else {
                                                    if ((*(*r20 + "tils.framework/Versions/A/AvidCoreUtils"))(r20) == 0x3) {
                                                            if (AComposition::GetMediaDesc() != 0x0) {
                                                                    r0 = __dynamic_cast();
                                                                    if (r0 != 0x0) {
                                                                            if ((*(*r0 + 0x2a0))() >= 0x1) {
                                                                                    if (AComposition::GetMediaDesc() != 0x0) {
                                                                                            r0 = __dynamic_cast();
                                                                                            if (r0 != 0x0) {
                                                                                                    if ((*(*r0 + 0x2a0))() == 0x0) {
                                                                                                            if (r19 != 0x0) {
                                                                                                                    AComposition::GetMobID();
                                                                                                                    (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                                                                                            }
                                                                                                            r0 = 0x3;
                                                                                                    }
                                                                                                    else {
                                                                                                            r0 = 0x2;
                                                                                                    }
                                                                                            }
                                                                                            else {
                                                                                                    r0 = 0x2;
                                                                                            }
                                                                                    }
                                                                                    else {
                                                                                            r0 = 0x2;
                                                                                    }
                                                                            }
                                                                            else {
                                                                                    r0 = 0x2;
                                                                            }
                                                                    }
                                                                    else {
                                                                            r0 = 0x2;
                                                                    }
                                                            }
                                                            else {
                                                                    r0 = 0x2;
                                                            }
                                                    }
                                                    else {
                                                            r0 = 0x2;
                                                    }
                                                    if (**___stack_chk_guard != var_58) {
                                                            __stack_chk_fail();
                                                    }
                                            }
                                    }
                                    else {
                                            if ((*(*r20 + "tils.framework/Versions/A/AvidCoreUtils"))(r20) == 0x3) {
                                                    if (AComposition::GetMediaDesc() != 0x0) {
                                                            r0 = __dynamic_cast();
                                                            if (r0 != 0x0) {
                                                                    if ((*(*r0 + 0x2a0))() >= 0x1) {
                                                                            if (AComposition::GetMediaDesc() != 0x0) {
                                                                                    r0 = __dynamic_cast();
                                                                                    if (r0 != 0x0) {
                                                                                            if ((*(*r0 + 0x2a0))() == 0x0) {
                                                                                                    if (r19 != 0x0) {
                                                                                                            AComposition::GetMobID();
                                                                                                            (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                                                                                    }
                                                                                                    r0 = 0x3;
                                                                                            }
                                                                                            else {
                                                                                                    r0 = 0x2;
                                                                                            }
                                                                                    }
                                                                                    else {
                                                                                            r0 = 0x2;
                                                                                    }
                                                                            }
                                                                            else {
                                                                                    r0 = 0x2;
                                                                            }
                                                                    }
                                                                    else {
                                                                            r0 = 0x2;
                                                                    }
                                                            }
                                                            else {
                                                                    r0 = 0x2;
                                                            }
                                                    }
                                                    else {
                                                            r0 = 0x2;
                                                    }
                                            }
                                            else {
                                                    r0 = 0x2;
                                            }
                                            if (**___stack_chk_guard != var_58) {
                                                    __stack_chk_fail();
                                            }
                                    }
                            }
                            else {
                                    XError::XError();
                                    if ((r22 | r23) == 0x0 && (*(*r20 + 0x1f8))(r20) > (*(*r21 + 0x1f8))(r21)) {
                                            if ((*(*r20 + 0xae8))(r20) == 0x0) {
                                                    if ((*(*r21 + 0xae8))(r21) != 0x0) {
                                                            XError::XError();
                                                            r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                            XError::~XError();
                                                            r22 = ResolveMerge(r20, r23, 0x1);
                                                    }
                                                    else {
                                                            r22 = ResolveMerge(r21, r20, 0x0);
                                                            r23 = 0x0;
                                                    }
                                            }
                                            else {
                                                    XError::XError();
                                                    r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                    XError::~XError();
                                                    r22 = ResolveMerge(r20, r23, 0x1);
                                            }
                                    }
                                    else {
                                            XError::XError();
                                            r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                            XError::~XError();
                                            r22 = ResolveMerge(r20, r23, 0x1);
                                    }
                                    r0 = StSetCurrentException::StSetCurrentException(&decomp_var_c8);
                                    if (r22 != 0x0) {
                                            if (var_90 != 0x0) {
                                                    if (r23 != 0x0) {
                                                            (**r23)(r23);
                                                            r23 = 0x0;
                                                    }
                                            }
                                    }
                                    else {
                                            if (r23 != 0x0) {
                                                    (**r23)(r23);
                                                    r23 = 0x0;
                                            }
                                    }
                                    if (var_90 != 0x0) {
                                            r0 = xraisexerror(&decomp_var_a0);
                                    }
                                    StSetCurrentException::~StSetCurrentException();
                                    XError::~XError();
                                    if (r22 != 0x0) {
                                            (*(*GetMobManProtocol() + 0xe0))();
                                            if (r19 != 0x0) {
                                                    AComposition::GetMobID();
                                                    (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                            }
                                            r0 = 0x3;
                                            if (**___stack_chk_guard != var_58) {
                                                    r0 = 0x3;
                                                    __stack_chk_fail();
                                            }
                                    }
                                    else {
                                            if ((*(*r20 + "tils.framework/Versions/A/AvidCoreUtils"))(r20) == 0x3) {
                                                    if (AComposition::GetMediaDesc() != 0x0) {
                                                            r0 = __dynamic_cast();
                                                            if (r0 != 0x0) {
                                                                    if ((*(*r0 + 0x2a0))() >= 0x1) {
                                                                            if (AComposition::GetMediaDesc() != 0x0) {
                                                                                    r0 = __dynamic_cast();
                                                                                    if (r0 != 0x0) {
                                                                                            if ((*(*r0 + 0x2a0))() == 0x0) {
                                                                                                    if (r19 != 0x0) {
                                                                                                            AComposition::GetMobID();
                                                                                                            (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                                                                                    }
                                                                                                    r0 = 0x3;
                                                                                            }
                                                                                            else {
                                                                                                    r0 = 0x2;
                                                                                            }
                                                                                    }
                                                                                    else {
                                                                                            r0 = 0x2;
                                                                                    }
                                                                            }
                                                                            else {
                                                                                    r0 = 0x2;
                                                                            }
                                                                    }
                                                                    else {
                                                                            r0 = 0x2;
                                                                    }
                                                            }
                                                            else {
                                                                    r0 = 0x2;
                                                            }
                                                    }
                                                    else {
                                                            r0 = 0x2;
                                                    }
                                            }
                                            else {
                                                    r0 = 0x2;
                                            }
                                            if (**___stack_chk_guard != var_58) {
                                                    __stack_chk_fail();
                                            }
                                    }
                            }
                    }
            }
    }
    else {
            if (r0 != 0x0) {
                    if (r22 == 0x0) {
                            MigrateSomePendingInfoToFinalMob(r21, r20);
                            if (r19 != 0x0) {
                                    AComposition::GetMobID();
                                    (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                            }
                            r0 = 0x3;
                            if (**___stack_chk_guard != var_58) {
                                    r0 = 0x3;
                                    __stack_chk_fail();
                            }
                    }
                    else {
                            r22 = (*(*r21 + "/../Frameworks/AvidCoreServices.framework/Versions/A/AvidCoreServices"))(r21);
                            r23 = (*(*r20 + "/../Frameworks/AvidCoreServices.framework/Versions/A/AvidCoreServices"))(r20);
                            r24 = CMGetGeneration(r21);
                            r25 = CMGetGeneration(r20);
                            if ((*(*r20 + "tils.framework/Versions/A/AvidCoreUtils"))(r20) == 0x3 && AComposition::GetMediaDesc() != 0x0) {
                                    if (AMediaDesc::GetPhysMobType() == 0x2) {
                                            r26 = 0x1;
                                    }
                            }
                            else {
                                    r26 = 0x0;
                            }
                            if (r25 <= r24) {
                                    r8 = *r20;
                                    r8 = r8["tils.framework/Versions/A/AvidCoreUtils"];
                                    if ((r8)(r20) != 0x2) {
                                            r8 = 0x1;
                                    }
                                    if ((r8 & !r26 & 0x1) == 0x0) {
                                            XError::XError();
                                            if ((r22 | r23) == 0x0 && (*(*r20 + 0x1f8))(r20) > (*(*r21 + 0x1f8))(r21)) {
                                                    if ((*(*r20 + 0xae8))(r20) == 0x0) {
                                                            if ((*(*r21 + 0xae8))(r21) != 0x0) {
                                                                    XError::XError();
                                                                    r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                                    XError::~XError();
                                                                    r22 = ResolveMerge(r20, r23, 0x1);
                                                            }
                                                            else {
                                                                    r22 = ResolveMerge(r21, r20, 0x0);
                                                                    r23 = 0x0;
                                                            }
                                                    }
                                                    else {
                                                            XError::XError();
                                                            r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                            XError::~XError();
                                                            r22 = ResolveMerge(r20, r23, 0x1);
                                                    }
                                            }
                                            else {
                                                    XError::XError();
                                                    r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                    XError::~XError();
                                                    r22 = ResolveMerge(r20, r23, 0x1);
                                            }
                                            r0 = StSetCurrentException::StSetCurrentException(&decomp_var_c8);
                                            if (r22 != 0x0) {
                                                    if (var_90 != 0x0) {
                                                            if (r23 != 0x0) {
                                                                    (**r23)(r23);
                                                                    r23 = 0x0;
                                                            }
                                                    }
                                            }
                                            else {
                                                    if (r23 != 0x0) {
                                                            (**r23)(r23);
                                                            r23 = 0x0;
                                                    }
                                            }
                                            if (var_90 != 0x0) {
                                                    r0 = xraisexerror(&decomp_var_a0);
                                            }
                                            StSetCurrentException::~StSetCurrentException();
                                            XError::~XError();
                                            if (r22 != 0x0) {
                                                    (*(*GetMobManProtocol() + 0xe0))();
                                                    if (r19 != 0x0) {
                                                            AComposition::GetMobID();
                                                            (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                                    }
                                                    r0 = 0x3;
                                                    if (**___stack_chk_guard != var_58) {
                                                            r0 = 0x3;
                                                            __stack_chk_fail();
                                                    }
                                            }
                                            else {
                                                    if ((*(*r20 + "tils.framework/Versions/A/AvidCoreUtils"))(r20) == 0x3) {
                                                            if (AComposition::GetMediaDesc() != 0x0) {
                                                                    r0 = __dynamic_cast();
                                                                    if (r0 != 0x0) {
                                                                            if ((*(*r0 + 0x2a0))() >= 0x1) {
                                                                                    if (AComposition::GetMediaDesc() != 0x0) {
                                                                                            r0 = __dynamic_cast();
                                                                                            if (r0 != 0x0) {
                                                                                                    if ((*(*r0 + 0x2a0))() == 0x0) {
                                                                                                            if (r19 != 0x0) {
                                                                                                                    AComposition::GetMobID();
                                                                                                                    (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                                                                                            }
                                                                                                            r0 = 0x3;
                                                                                                    }
                                                                                                    else {
                                                                                                            r0 = 0x2;
                                                                                                    }
                                                                                            }
                                                                                            else {
                                                                                                    r0 = 0x2;
                                                                                            }
                                                                                    }
                                                                                    else {
                                                                                            r0 = 0x2;
                                                                                    }
                                                                            }
                                                                            else {
                                                                                    r0 = 0x2;
                                                                            }
                                                                    }
                                                                    else {
                                                                            r0 = 0x2;
                                                                    }
                                                            }
                                                            else {
                                                                    r0 = 0x2;
                                                            }
                                                    }
                                                    else {
                                                            r0 = 0x2;
                                                    }
                                                    if (**___stack_chk_guard != var_58) {
                                                            __stack_chk_fail();
                                                    }
                                            }
                                    }
                                    else {
                                            if ((*(*r20 + "tils.framework/Versions/A/AvidCoreUtils"))(r20) == 0x3) {
                                                    if (AComposition::GetMediaDesc() != 0x0) {
                                                            r0 = __dynamic_cast();
                                                            if (r0 != 0x0) {
                                                                    if ((*(*r0 + 0x2a0))() >= 0x1) {
                                                                            if (AComposition::GetMediaDesc() != 0x0) {
                                                                                    r0 = __dynamic_cast();
                                                                                    if (r0 != 0x0) {
                                                                                            if ((*(*r0 + 0x2a0))() == 0x0) {
                                                                                                    if (r19 != 0x0) {
                                                                                                            AComposition::GetMobID();
                                                                                                            (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                                                                                    }
                                                                                                    r0 = 0x3;
                                                                                            }
                                                                                            else {
                                                                                                    r0 = 0x2;
                                                                                            }
                                                                                    }
                                                                                    else {
                                                                                            r0 = 0x2;
                                                                                    }
                                                                            }
                                                                            else {
                                                                                    r0 = 0x2;
                                                                            }
                                                                    }
                                                                    else {
                                                                            r0 = 0x2;
                                                                    }
                                                            }
                                                            else {
                                                                    r0 = 0x2;
                                                            }
                                                    }
                                                    else {
                                                            r0 = 0x2;
                                                    }
                                            }
                                            else {
                                                    r0 = 0x2;
                                            }
                                            if (**___stack_chk_guard != var_58) {
                                                    __stack_chk_fail();
                                            }
                                    }
                            }
                            else {
                                    XError::XError();
                                    if ((r22 | r23) == 0x0 && (*(*r20 + 0x1f8))(r20) > (*(*r21 + 0x1f8))(r21)) {
                                            if ((*(*r20 + 0xae8))(r20) == 0x0) {
                                                    if ((*(*r21 + 0xae8))(r21) != 0x0) {
                                                            XError::XError();
                                                            r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                            XError::~XError();
                                                            r22 = ResolveMerge(r20, r23, 0x1);
                                                    }
                                                    else {
                                                            r22 = ResolveMerge(r21, r20, 0x0);
                                                            r23 = 0x0;
                                                    }
                                            }
                                            else {
                                                    XError::XError();
                                                    r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                    XError::~XError();
                                                    r22 = ResolveMerge(r20, r23, 0x1);
                                            }
                                    }
                                    else {
                                            XError::XError();
                                            r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                            XError::~XError();
                                            r22 = ResolveMerge(r20, r23, 0x1);
                                    }
                                    r0 = StSetCurrentException::StSetCurrentException(&decomp_var_c8);
                                    if (r22 != 0x0) {
                                            if (var_90 != 0x0) {
                                                    if (r23 != 0x0) {
                                                            (**r23)(r23);
                                                            r23 = 0x0;
                                                    }
                                            }
                                    }
                                    else {
                                            if (r23 != 0x0) {
                                                    (**r23)(r23);
                                                    r23 = 0x0;
                                            }
                                    }
                                    if (var_90 != 0x0) {
                                            r0 = xraisexerror(&decomp_var_a0);
                                    }
                                    StSetCurrentException::~StSetCurrentException();
                                    XError::~XError();
                                    if (r22 != 0x0) {
                                            (*(*GetMobManProtocol() + 0xe0))();
                                            if (r19 != 0x0) {
                                                    AComposition::GetMobID();
                                                    (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                            }
                                            r0 = 0x3;
                                            if (**___stack_chk_guard != var_58) {
                                                    r0 = 0x3;
                                                    __stack_chk_fail();
                                            }
                                    }
                                    else {
                                            if ((*(*r20 + "tils.framework/Versions/A/AvidCoreUtils"))(r20) == 0x3) {
                                                    if (AComposition::GetMediaDesc() != 0x0) {
                                                            r0 = __dynamic_cast();
                                                            if (r0 != 0x0) {
                                                                    if ((*(*r0 + 0x2a0))() >= 0x1) {
                                                                            if (AComposition::GetMediaDesc() != 0x0) {
                                                                                    r0 = __dynamic_cast();
                                                                                    if (r0 != 0x0) {
                                                                                            if ((*(*r0 + 0x2a0))() == 0x0) {
                                                                                                    if (r19 != 0x0) {
                                                                                                            AComposition::GetMobID();
                                                                                                            (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                                                                                    }
                                                                                                    r0 = 0x3;
                                                                                            }
                                                                                            else {
                                                                                                    r0 = 0x2;
                                                                                            }
                                                                                    }
                                                                                    else {
                                                                                            r0 = 0x2;
                                                                                    }
                                                                            }
                                                                            else {
                                                                                    r0 = 0x2;
                                                                            }
                                                                    }
                                                                    else {
                                                                            r0 = 0x2;
                                                                    }
                                                            }
                                                            else {
                                                                    r0 = 0x2;
                                                            }
                                                    }
                                                    else {
                                                            r0 = 0x2;
                                                    }
                                            }
                                            else {
                                                    r0 = 0x2;
                                            }
                                            if (**___stack_chk_guard != var_58) {
                                                    __stack_chk_fail();
                                            }
                                    }
                            }
                    }
            }
            else {
                    r22 = (*(*r21 + "/../Frameworks/AvidCoreServices.framework/Versions/A/AvidCoreServices"))(r21);
                    r23 = (*(*r20 + "/../Frameworks/AvidCoreServices.framework/Versions/A/AvidCoreServices"))(r20);
                    r24 = CMGetGeneration(r21);
                    r25 = CMGetGeneration(r20);
                    if ((*(*r20 + "tils.framework/Versions/A/AvidCoreUtils"))(r20) == 0x3 && AComposition::GetMediaDesc() != 0x0) {
                            if (AMediaDesc::GetPhysMobType() == 0x2) {
                                    r26 = 0x1;
                            }
                    }
                    else {
                            r26 = 0x0;
                    }
                    if (r25 <= r24) {
                            r8 = *r20;
                            r8 = r8["tils.framework/Versions/A/AvidCoreUtils"];
                            if ((r8)(r20) != 0x2) {
                                    r8 = 0x1;
                            }
                            if ((r8 & !r26 & 0x1) == 0x0) {
                                    XError::XError();
                                    if ((r22 | r23) == 0x0 && (*(*r20 + 0x1f8))(r20) > (*(*r21 + 0x1f8))(r21) && (*(*r20 + 0xae8))(r20) == 0x0) {
                                            if ((*(*r21 + 0xae8))(r21) != 0x0) {
                                                    XError::XError();
                                                    r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                    XError::~XError();
                                                    r22 = ResolveMerge(r20, r23, 0x1);
                                            }
                                            else {
                                                    r22 = ResolveMerge(r21, r20, 0x0);
                                                    r23 = 0x0;
                                            }
                                    }
                                    else {
                                            XError::XError();
                                            r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                            XError::~XError();
                                            r22 = ResolveMerge(r20, r23, 0x1);
                                    }
                                    r0 = StSetCurrentException::StSetCurrentException(&decomp_var_c8);
                                    if (r22 == 0x0 || var_90 != 0x0) {
                                            if (r23 != 0x0) {
                                                    (**r23)(r23);
                                                    r23 = 0x0;
                                            }
                                    }
                                    if (var_90 != 0x0) {
                                            r0 = xraisexerror(&decomp_var_a0);
                                    }
                                    StSetCurrentException::~StSetCurrentException();
                                    XError::~XError();
                                    if (r22 != 0x0) {
                                            (*(*GetMobManProtocol() + 0xe0))();
                                            if (r19 != 0x0) {
                                                    AComposition::GetMobID();
                                                    (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                            }
                                            r0 = 0x3;
                                            if (**___stack_chk_guard != var_58) {
                                                    r0 = 0x3;
                                                    __stack_chk_fail();
                                            }
                                    }
                                    else {
                                            if ((*(*r20 + "tils.framework/Versions/A/AvidCoreUtils"))(r20) == 0x3) {
                                                    if (AComposition::GetMediaDesc() != 0x0) {
                                                            r0 = __dynamic_cast();
                                                            if (r0 != 0x0) {
                                                                    if ((*(*r0 + 0x2a0))() >= 0x1) {
                                                                            if (AComposition::GetMediaDesc() != 0x0) {
                                                                                    r0 = __dynamic_cast();
                                                                                    if (r0 != 0x0) {
                                                                                            if ((*(*r0 + 0x2a0))() == 0x0) {
                                                                                                    if (r19 != 0x0) {
                                                                                                            AComposition::GetMobID();
                                                                                                            (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                                                                                    }
                                                                                                    r0 = 0x3;
                                                                                            }
                                                                                            else {
                                                                                                    r0 = 0x2;
                                                                                            }
                                                                                    }
                                                                                    else {
                                                                                            r0 = 0x2;
                                                                                    }
                                                                            }
                                                                            else {
                                                                                    r0 = 0x2;
                                                                            }
                                                                    }
                                                                    else {
                                                                            r0 = 0x2;
                                                                    }
                                                            }
                                                            else {
                                                                    r0 = 0x2;
                                                            }
                                                    }
                                                    else {
                                                            r0 = 0x2;
                                                    }
                                            }
                                            else {
                                                    r0 = 0x2;
                                            }
                                            if (**___stack_chk_guard != var_58) {
                                                    __stack_chk_fail();
                                            }
                                    }
                            }
                            else {
                                    if ((*(*r20 + "tils.framework/Versions/A/AvidCoreUtils"))(r20) == 0x3) {
                                            if (AComposition::GetMediaDesc() != 0x0) {
                                                    r0 = __dynamic_cast();
                                                    if (r0 != 0x0) {
                                                            if ((*(*r0 + 0x2a0))() >= 0x1) {
                                                                    if (AComposition::GetMediaDesc() != 0x0) {
                                                                            r0 = __dynamic_cast();
                                                                            if (r0 != 0x0) {
                                                                                    if ((*(*r0 + 0x2a0))() == 0x0) {
                                                                                            if (r19 != 0x0) {
                                                                                                    AComposition::GetMobID();
                                                                                                    (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                                                                            }
                                                                                            r0 = 0x3;
                                                                                    }
                                                                                    else {
                                                                                            r0 = 0x2;
                                                                                    }
                                                                            }
                                                                            else {
                                                                                    r0 = 0x2;
                                                                            }
                                                                    }
                                                                    else {
                                                                            r0 = 0x2;
                                                                    }
                                                            }
                                                            else {
                                                                    r0 = 0x2;
                                                            }
                                                    }
                                                    else {
                                                            r0 = 0x2;
                                                    }
                                            }
                                            else {
                                                    r0 = 0x2;
                                            }
                                    }
                                    else {
                                            r0 = 0x2;
                                    }
                                    if (**___stack_chk_guard != var_58) {
                                            __stack_chk_fail();
                                    }
                            }
                    }
                    else {
                            XError::XError();
                            if ((r22 | r23) == 0x0) {
                                    if ((*(*r20 + 0x1f8))(r20) > (*(*r21 + 0x1f8))(r21)) {
                                            if ((*(*r20 + 0xae8))(r20) == 0x0) {
                                                    if ((*(*r21 + 0xae8))(r21) != 0x0) {
                                                            XError::XError();
                                                            r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                            XError::~XError();
                                                            r22 = ResolveMerge(r20, r23, 0x1);
                                                    }
                                                    else {
                                                            r22 = ResolveMerge(r21, r20, 0x0);
                                                            r23 = 0x0;
                                                    }
                                            }
                                            else {
                                                    XError::XError();
                                                    r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                                    XError::~XError();
                                                    r22 = ResolveMerge(r20, r23, 0x1);
                                            }
                                    }
                                    else {
                                            XError::XError();
                                            r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                            XError::~XError();
                                            r22 = ResolveMerge(r20, r23, 0x1);
                                    }
                            }
                            else {
                                    XError::XError();
                                    r23 = (*(*r21 + "eworks/lavaFlow.framework/Versions/A/lavaFlow"))(r21);
                                    XError::~XError();
                                    r22 = ResolveMerge(r20, r23, 0x1);
                            }
                            r0 = StSetCurrentException::StSetCurrentException(&decomp_var_c8);
                            if (r22 != 0x0) {
                                    if (var_90 != 0x0 && r23 != 0x0) {
                                            (**r23)(r23);
                                            r23 = 0x0;
                                    }
                            }
                            else {
                                    if (r23 != 0x0) {
                                            (**r23)(r23);
                                            r23 = 0x0;
                                    }
                            }
                            if (var_90 != 0x0) {
                                    r0 = xraisexerror(&decomp_var_a0);
                            }
                            StSetCurrentException::~StSetCurrentException();
                            XError::~XError();
                            if (r22 != 0x0) {
                                    (*(*GetMobManProtocol() + 0xe0))();
                                    if (r19 != 0x0) {
                                            AComposition::GetMobID();
                                            (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                    }
                                    r0 = 0x3;
                                    if (**___stack_chk_guard != var_58) {
                                            r0 = 0x3;
                                            __stack_chk_fail();
                                    }
                            }
                            else {
                                    if ((*(*r20 + "tils.framework/Versions/A/AvidCoreUtils"))(r20) == 0x3) {
                                            if (AComposition::GetMediaDesc() != 0x0) {
                                                    r0 = __dynamic_cast();
                                                    if (r0 != 0x0) {
                                                            if ((*(*r0 + 0x2a0))() >= 0x1) {
                                                                    if (AComposition::GetMediaDesc() != 0x0) {
                                                                            r0 = __dynamic_cast();
                                                                            if (r0 != 0x0) {
                                                                                    if ((*(*r0 + 0x2a0))() == 0x0) {
                                                                                            if (r19 != 0x0) {
                                                                                                    AComposition::GetMobID();
                                                                                                    (*(*r19 + 0x68))(r19, &decomp_var_78, 0x7fffffff);
                                                                                            }
                                                                                            r0 = 0x3;
                                                                                    }
                                                                                    else {
                                                                                            r0 = 0x2;
                                                                                    }
                                                                            }
                                                                            else {
                                                                                    r0 = 0x2;
                                                                            }
                                                                    }
                                                                    else {
                                                                            r0 = 0x2;
                                                                    }
                                                            }
                                                            else {
                                                                    r0 = 0x2;
                                                            }
                                                    }
                                                    else {
                                                            r0 = 0x2;
                                                    }
                                            }
                                            else {
                                                    r0 = 0x2;
                                            }
                                    }
                                    else {
                                            r0 = 0x2;
                                    }
                                    if (**___stack_chk_guard != var_58) {
                                            __stack_chk_fail();
                                    }
                            }
                    }
            }
    }
    return r0;
