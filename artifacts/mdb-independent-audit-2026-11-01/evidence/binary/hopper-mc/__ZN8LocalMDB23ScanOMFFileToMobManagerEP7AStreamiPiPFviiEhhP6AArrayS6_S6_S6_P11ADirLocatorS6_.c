    r7 = arg7;
    r4 = arg4;
    r2 = arg2;
    r0 = arg0;
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
    r31 = r31 + 0xffffffffffffffa0 - 0x5d0;
    var_5E8 = arg6;
    var_5D4 = arg1;
    var_5D0 = arg5;
    var_5C8 = arg3;
    var_60 = **___stack_chk_guard;
    if (r0 == 0x0) goto loc_4150b8;
    goto loc_415018;

loc_4150b8:
    r19 = 0x6;
    goto loc_415a10;

loc_415018:
    r19 = r7;
    r26 = r4;
    r24 = r2;
    r22 = r0;
    r0 = GetMobMan();
    if (r0 == 0x0) goto loc_4150c0;
    goto loc_415030;

loc_415a10:
    if (**___stack_chk_guard != var_60) {
            __stack_chk_fail();
    }
    r0 = r19;
    return r0;

loc_4150c0:
    r19 = 0x7;
    goto loc_415a10;

loc_415030:
    XError::XError();
    r27 = 0x1;
    var_5F8 = r0;
    (*(*r0 + 0xb0))(r0);
    (*(*r22 + 0xc8))(r22, 0x20000, 0x40000);
    r0 = omfiHPDomain::New(r22, 0x0, 0x1, 0x0);
    r20 = r0;
    if (r0 == 0x0) goto loc_4150c8;
    goto loc_4150a0;

loc_4150c8:
    r21 = 0x0;
    r23 = 0x0;
    goto loc_41548c;

loc_4150a0:
    r21 = arg_0;
    var_600 = arg_10;
    if (arg_10 != 0x0) {
            if (r21 != 0x0 && r21[0x1] <= "vid.72.1.dylib") {
                    r0 = (*(*r20 + 0x1d0))(r20);
                    if (r0 >= "id.72.1.dylib") {
                            (*(*r21 + 0xd8))(r21, r0 & 0xfffffffffffff800);
                    }
            }
    }
    else {
            r20[0x32] = 0x1;
            if (r21 != 0x0 && r21[0x1] <= "vid.72.1.dylib") {
                    r0 = (*(*r20 + 0x1d0))(r20);
                    if (r0 >= "id.72.1.dylib") {
                            (*(*r21 + 0xd8))(r21, r0 & 0xfffffffffffff800);
                    }
            }
    }
    var_5E0 = r21;
    (*(*r20 + 0x268))(r20, 0x2);
    r0 = (*(*r20 + 0x240))(r20, 0x2);
    r25 = r0;
    if (r0 == 0x0) goto loc_415474;
    goto loc_415150;

loc_41548c:
    r0 = StSetCurrentException::StSetCurrentException(&decomp_var_5c0);
    r19 = var_5A8;
    XError::XError();
    r22 = var_5F8;
    if (r20 != 0x0) {
            (**r20)(r20);
    }
    if (r19 != 0x0) {
            if (dbgTestFlag(*(int32_t *)AMediaStreamMgr::MSMDbgID, 0x2) != 0x0) {
                    if (r21 != 0x0) {
                            r19 = &var_588;
                            (*(*r21 + 0x2b0))(r21, &var_588, 0x64);
                            r8 = "physical";
                            if (r27 == 0x0) {
                                    r8 = "logical";
                            }
                            dprintf1("Failure occurred loading %s mob %d, \"%s\"\n");
                    }
                    else {
                            dprintf1("Failure occurred loading mob %d\n");
                    }
            }
            (*(*r22 + 0xf8))(r22);
            r19 = 0x1;
    }
    else {
            (*(*r22 + 0xd0))(r22, LocalMDB::MobConflictResolver(AComposition*, AComposition*, void*), arg_8);
            (*(*r22 + 0xe8))(r22);
            r19 = 0x0;
    }
    XError::~XError();
    r0 = XError::FindCode(&decomp_var_5b8);
    if (r0 != 0x0) {
            r19 = 0x4;
    }
    else {
            if (var_5A8 != 0x0) {
                    r0 = xraisexerror(&decomp_var_5b8);
            }
    }
    StSetCurrentException::~StSetCurrentException();
    r0 = XError::~XError();
    goto loc_415a10;

loc_415474:
    r21 = 0x0;
    goto loc_415478;

loc_415150:
    r23 = 0x0;
    var_5F0 = arg_18;
    goto loc_415160;

loc_415478:
    if ((var_5D0 | r26) != 0x0) {
            r27 = 0x0;
            r23 = 0x0;
    }
    else {
            (*(*r20 + 0x268))(r20, 0x1);
            r27 = 0x0;
            r0 = (*(*r20 + 0x240))(r20, 0x1);
            r19 = r0;
            if (r0 != 0x0) {
                    r23 = 0x0;
                    if ((var_5E8 | var_5E0) != 0x0) {
                            r27 = 0x1;
                    }
                    do {
                            r28 = r23 + 0x1;
                            r22 = (*(*r20 + 0x258))(r20, 0x1, r28);
                            XError::XError();
                            r0 = (*(*r20 + 0x238))(r20, r22);
                            r21 = r0;
                            if (r0 == 0x0) {
                                    XError::XError(&var_588);
                                    r0 = operator+(&var_588);
                                    XError::~XError();
                                    r21 = 0x0;
                            }
                            XError::~XError();
                            (*(*r21 + "@executable_path/../Frameworks/AvidCoreServices.framework/Versions/A/AvidCoreServices"))(r21, 0x0);
                            if (r27 != 0x0) {
                                    AComposition::GetMobID();
                                    var_80 = var_588;
                                    r0 = var_5E8;
                                    if (r0 != 0x0) {
                                            (*(*r0 + 0x68))();
                                    }
                                    else {
                                            if (var_5E0 != 0x0) {
                                                    r0 = (*(*var_5E0 + 0x118))(var_5E0, &var_80, AbstractMDBCompareMobID(long, void*), &var_590, 0x0);
                                                    if (r0 == 0x0) {
                                                            r0 = var_5E0;
                                                            (*(*r0 + 0x68))();
                                                    }
                                            }
                                    }
                            }
                            if (var_5C8 != 0x0) {
                                    r26 = 0x51eb850;
                                    r8 = *(int32_t *)r24;
                                    r1 = r8 + 0x1;
                                    *(int32_t *)r24 = r1;
                                    r8 = 0xc28f5c29 + r8 * r1;
                                    asm { ror        w8, w8, #0x1 };
                                    if (r8 <= r26) {
                                            (var_5C8)(var_5D4, r1);
                                    }
                            }
                            r23 = r28;
                    } while (r19 != r28);
                    r27 = 0x0;
                    r23 = r19;
            }
            else {
                    r27 = 0x0;
                    r23 = 0x0;
            }
    }
    goto loc_41548c;

loc_415160:
    r27 = r23 + 0x1;
    r0 = (*(*r20 + 0x258))(r20, 0x2, r27);
    r0 = (*(*r20 + 0x238))(r20, r0);
    r21 = r0;
    if (r0 == 0x0) {
            XError::XError(&var_588);
            r0 = operator+(&var_588);
            XError::~XError();
    }
    (*(*r21 + "@executable_path/../Frameworks/AvidCoreServices.framework/Versions/A/AvidCoreServices"))(r21, 0x0);
    r22 = AComposition::GetMediaDesc();
    if (var_5C8 != 0x0) {
            r1 = *(int32_t *)r24 + 0x1;
            *(int32_t *)r24 = r1;
            r9 = 0x51eb850;
            r8 = 0xc28f5c29 + 0xc28f5c29 * r1;
            asm { ror        w8, w8, #0x1 };
            if (r8 <= r9) {
                    (var_5C8)(var_5D4, r1);
            }
    }
    if (r26 != 0x0) {
            if (r22 != 0x0) {
                    if (AMediaDesc::GetPhysMobType() != 0x1) {
                            if (AComposition::GetAppCode() == 0xe) {
                                    (**r21)(r21);
                                    r23 = r27;
                                    if (r25 != r27) {
                                            continue;
                                    }
                                    break;
                            }
                            else {
                                    AComposition::GetMobID();
                                    var_80 = var_588;
                                    decomp_var_70 = var_578;
                                    if (r19 != 0x0) {
                                            r0 = (*(*r19 + 0x68))(r19, &var_80, r19[0x1]);
                                    }
                                    r0 = IsWellKnownMobID(&var_80);
                                    if (r0 == 0x0 && r22 != 0x0) {
                                            if (AMediaDesc::GetPhysMobType() == 0x1) {
                                                    r0 = AMediaDesc::GetLocator();
                                                    r28 = r0;
                                                    if (r0 != 0x0) {
                                                            if ((*(*r28 + 0x20))(r28, "MSMLocator") != 0x0) {
                                                                    if (var_600 == 0x0) {
                                                                            r0 = __dynamic_cast();
                                                                            r22 = r0;
                                                                            if (r0 == 0x0 && __FtlAssertFailed("msmLoc", "/Volumes/Media/Builds/workspace/MC/MCRelease_Installer/Mac_Build/coresw/ame/src/msmdb/LocalMDB.c", "AvidCoreServices.framework/Versions/A/AvidCoreServices") != 0x0) {
                                                                                    DebugStr("\x11FtlAssert called.");
                                                                            }
                                                                            if ((*(*r22 + 0x1c0))(r22) == 0x0) {
                                                                                    (*(*r22 + 0x1c8))(r22, 0x1);
                                                                            }
                                                                    }
                                                            }
                                                            else {
                                                                    (**r28)(r28);
                                                                    var_B0 = var_80;
                                                                    r0 = MSMLocator::New(&var_B0, 0x1, 0x0);
                                                                    (*(*r22 + 0x140))(r22, r0);
                                                            }
                                                    }
                                                    else {
                                                            var_B0 = var_80;
                                                            r0 = MSMLocator::New(&var_B0, 0x1, 0x0);
                                                            (*(*r22 + 0x140))(r22, r0);
                                                    }
                                                    if (var_5F0 != 0x0) {
                                                            AComposition::GetMobID();
                                                            r0 = (*(*r20 + 0x248))(r20, &var_B0, 0x0);
                                                            if (r0 != 0x0) {
                                                                    r0 = (*(*var_5F0 + 0x68))(var_5F0, &var_80, var_5F0[0x1] + 0x1);
                                                            }
                                                    }
                                                    if (var_5E0 != 0x0) {
                                                            r0 = (*(*var_5E0 + 0x118))(var_5E0, &var_80, AbstractMDBCompareMobID(long, void*), &var_590, 0x0);
                                                            if (r0 == 0x0) {
                                                                    (*(*var_5E0 + 0x68))(var_5E0, &var_80, var_590);
                                                            }
                                                    }
                                            }
                                    }
                                    r23 = r27;
                                    if (r25 != r27) {
                                            continue;
                                    }
                                    break;
                            }
                    }
                    else {
                            (**r21)(r21);
                            r23 = r27;
                            if (r25 != r27) {
                                    continue;
                            }
                            break;
                    }
            }
            else {
                    if (AComposition::GetAppCode() == 0xe) {
                            (**r21)(r21);
                            r23 = r27;
                            if (r25 != r27) {
                                    continue;
                            }
                            break;
                    }
                    else {
                            AComposition::GetMobID();
                            var_80 = var_588;
                            decomp_var_70 = var_578;
                            if (r19 != 0x0) {
                                    r0 = (*(*r19 + 0x68))(r19, &var_80, r19[0x1]);
                            }
                            r0 = IsWellKnownMobID(&var_80);
                            if (r0 == 0x0 && r22 != 0x0) {
                                    if (AMediaDesc::GetPhysMobType() == 0x1) {
                                            r0 = AMediaDesc::GetLocator();
                                            r28 = r0;
                                            if (r0 != 0x0) {
                                                    if ((*(*r28 + 0x20))(r28, "MSMLocator") != 0x0) {
                                                            if (var_600 == 0x0) {
                                                                    r0 = __dynamic_cast();
                                                                    r22 = r0;
                                                                    if (r0 == 0x0 && __FtlAssertFailed("msmLoc", "/Volumes/Media/Builds/workspace/MC/MCRelease_Installer/Mac_Build/coresw/ame/src/msmdb/LocalMDB.c", "AvidCoreServices.framework/Versions/A/AvidCoreServices") != 0x0) {
                                                                            DebugStr("\x11FtlAssert called.");
                                                                    }
                                                                    if ((*(*r22 + 0x1c0))(r22) == 0x0) {
                                                                            (*(*r22 + 0x1c8))(r22, 0x1);
                                                                    }
                                                            }
                                                    }
                                                    else {
                                                            (**r28)(r28);
                                                            var_B0 = var_80;
                                                            r0 = MSMLocator::New(&var_B0, 0x1, 0x0);
                                                            (*(*r22 + 0x140))(r22, r0);
                                                    }
                                            }
                                            else {
                                                    var_B0 = var_80;
                                                    r0 = MSMLocator::New(&var_B0, 0x1, 0x0);
                                                    (*(*r22 + 0x140))(r22, r0);
                                            }
                                            if (var_5F0 != 0x0) {
                                                    AComposition::GetMobID();
                                                    r0 = (*(*r20 + 0x248))(r20, &var_B0, 0x0);
                                                    if (r0 != 0x0) {
                                                            r0 = (*(*var_5F0 + 0x68))(var_5F0, &var_80, var_5F0[0x1] + 0x1);
                                                    }
                                            }
                                            if (var_5E0 != 0x0) {
                                                    r0 = (*(*var_5E0 + 0x118))(var_5E0, &var_80, AbstractMDBCompareMobID(long, void*), &var_590, 0x0);
                                                    if (r0 == 0x0) {
                                                            (*(*var_5E0 + 0x68))(var_5E0, &var_80, var_590);
                                                    }
                                            }
                                    }
                            }
                            r23 = r27;
                            if (r25 != r27) {
                                    continue;
                            }
                            break;
                    }
            }
    }
    else {
            if (var_5D0 != 0x0) {
                    if (r22 != 0x0) {
                            if (AMediaDesc::GetPhysMobType() == 0x1) {
                                    AComposition::GetMobID();
                                    r0 = (*(*r20 + 0x248))(r20, &var_B0, 0x0);
                                    if (r0 != 0x0) {
                                            AComposition::GetMobID();
                                            var_80 = var_588;
                                            decomp_var_70 = var_578;
                                            if (r19 != 0x0) {
                                                    r0 = (*(*r19 + 0x68))(r19, &var_80, r19[0x1]);
                                            }
                                            r0 = IsWellKnownMobID(&var_80);
                                            if (r0 == 0x0 && r22 != 0x0) {
                                                    if (AMediaDesc::GetPhysMobType() == 0x1) {
                                                            r0 = AMediaDesc::GetLocator();
                                                            r28 = r0;
                                                            if (r0 != 0x0) {
                                                                    if ((*(*r28 + 0x20))(r28, "MSMLocator") != 0x0) {
                                                                            if (var_600 == 0x0) {
                                                                                    r0 = __dynamic_cast();
                                                                                    r22 = r0;
                                                                                    if (r0 == 0x0 && __FtlAssertFailed("msmLoc", "/Volumes/Media/Builds/workspace/MC/MCRelease_Installer/Mac_Build/coresw/ame/src/msmdb/LocalMDB.c", "AvidCoreServices.framework/Versions/A/AvidCoreServices") != 0x0) {
                                                                                            DebugStr("\x11FtlAssert called.");
                                                                                    }
                                                                                    if ((*(*r22 + 0x1c0))(r22) == 0x0) {
                                                                                            (*(*r22 + 0x1c8))(r22, 0x1);
                                                                                    }
                                                                            }
                                                                    }
                                                                    else {
                                                                            (**r28)(r28);
                                                                            var_B0 = var_80;
                                                                            r0 = MSMLocator::New(&var_B0, 0x1, 0x0);
                                                                            (*(*r22 + 0x140))(r22, r0);
                                                                    }
                                                            }
                                                            else {
                                                                    var_B0 = var_80;
                                                                    r0 = MSMLocator::New(&var_B0, 0x1, 0x0);
                                                                    (*(*r22 + 0x140))(r22, r0);
                                                            }
                                                            if (var_5F0 != 0x0) {
                                                                    AComposition::GetMobID();
                                                                    r0 = (*(*r20 + 0x248))(r20, &var_B0, 0x0);
                                                                    if (r0 != 0x0) {
                                                                            r0 = (*(*var_5F0 + 0x68))(var_5F0, &var_80, var_5F0[0x1] + 0x1);
                                                                    }
                                                            }
                                                            if (var_5E0 != 0x0) {
                                                                    r0 = (*(*var_5E0 + 0x118))(var_5E0, &var_80, AbstractMDBCompareMobID(long, void*), &var_590, 0x0);
                                                                    if (r0 == 0x0) {
                                                                            (*(*var_5E0 + 0x68))(var_5E0, &var_80, var_590);
                                                                    }
                                                            }
                                                    }
                                            }
                                            r23 = r27;
                                            if (r25 != r27) {
                                                    continue;
                                            }
                                            break;
                                    }
                                    else {
                                            (**r21)(r21);
                                            r23 = r27;
                                            if (r25 != r27) {
                                                    continue;
                                            }
                                            break;
                                    }
                            }
                            else {
                                    AComposition::GetMobID();
                                    var_80 = var_588;
                                    decomp_var_70 = var_578;
                                    if (r19 != 0x0) {
                                            r0 = (*(*r19 + 0x68))(r19, &var_80, r19[0x1]);
                                    }
                                    r0 = IsWellKnownMobID(&var_80);
                                    if (r0 == 0x0 && r22 != 0x0) {
                                            if (AMediaDesc::GetPhysMobType() == 0x1) {
                                                    r0 = AMediaDesc::GetLocator();
                                                    r28 = r0;
                                                    if (r0 != 0x0) {
                                                            if ((*(*r28 + 0x20))(r28, "MSMLocator") != 0x0) {
                                                                    if (var_600 == 0x0) {
                                                                            r0 = __dynamic_cast();
                                                                            r22 = r0;
                                                                            if (r0 == 0x0 && __FtlAssertFailed("msmLoc", "/Volumes/Media/Builds/workspace/MC/MCRelease_Installer/Mac_Build/coresw/ame/src/msmdb/LocalMDB.c", "AvidCoreServices.framework/Versions/A/AvidCoreServices") != 0x0) {
                                                                                    DebugStr("\x11FtlAssert called.");
                                                                            }
                                                                            if ((*(*r22 + 0x1c0))(r22) == 0x0) {
                                                                                    (*(*r22 + 0x1c8))(r22, 0x1);
                                                                            }
                                                                    }
                                                            }
                                                            else {
                                                                    (**r28)(r28);
                                                                    var_B0 = var_80;
                                                                    r0 = MSMLocator::New(&var_B0, 0x1, 0x0);
                                                                    (*(*r22 + 0x140))(r22, r0);
                                                            }
                                                    }
                                                    else {
                                                            var_B0 = var_80;
                                                            r0 = MSMLocator::New(&var_B0, 0x1, 0x0);
                                                            (*(*r22 + 0x140))(r22, r0);
                                                    }
                                                    if (var_5F0 != 0x0) {
                                                            AComposition::GetMobID();
                                                            r0 = (*(*r20 + 0x248))(r20, &var_B0, 0x0);
                                                            if (r0 != 0x0) {
                                                                    r0 = (*(*var_5F0 + 0x68))(var_5F0, &var_80, var_5F0[0x1] + 0x1);
                                                            }
                                                    }
                                                    if (var_5E0 != 0x0) {
                                                            r0 = (*(*var_5E0 + 0x118))(var_5E0, &var_80, AbstractMDBCompareMobID(long, void*), &var_590, 0x0);
                                                            if (r0 == 0x0) {
                                                                    (*(*var_5E0 + 0x68))(var_5E0, &var_80, var_590);
                                                            }
                                                    }
                                            }
                                    }
                                    r23 = r27;
                                    if (r25 != r27) {
                                            continue;
                                    }
                                    break;
                            }
                    }
                    else {
                            AComposition::GetMobID();
                            var_80 = var_588;
                            decomp_var_70 = var_578;
                            if (r19 != 0x0) {
                                    r0 = (*(*r19 + 0x68))(r19, &var_80, r19[0x1]);
                            }
                            r0 = IsWellKnownMobID(&var_80);
                            if (r0 == 0x0 && r22 != 0x0 && AMediaDesc::GetPhysMobType() == 0x1) {
                                    r0 = AMediaDesc::GetLocator();
                                    r28 = r0;
                                    if (r0 != 0x0) {
                                            if ((*(*r28 + 0x20))(r28, "MSMLocator") != 0x0) {
                                                    if (var_600 == 0x0) {
                                                            r0 = __dynamic_cast();
                                                            r22 = r0;
                                                            if (r0 == 0x0 && __FtlAssertFailed("msmLoc", "/Volumes/Media/Builds/workspace/MC/MCRelease_Installer/Mac_Build/coresw/ame/src/msmdb/LocalMDB.c", "AvidCoreServices.framework/Versions/A/AvidCoreServices") != 0x0) {
                                                                    DebugStr("\x11FtlAssert called.");
                                                            }
                                                            if ((*(*r22 + 0x1c0))(r22) == 0x0) {
                                                                    (*(*r22 + 0x1c8))(r22, 0x1);
                                                            }
                                                    }
                                            }
                                            else {
                                                    (**r28)(r28);
                                                    var_B0 = var_80;
                                                    r0 = MSMLocator::New(&var_B0, 0x1, 0x0);
                                                    (*(*r22 + 0x140))(r22, r0);
                                            }
                                    }
                                    else {
                                            var_B0 = var_80;
                                            r0 = MSMLocator::New(&var_B0, 0x1, 0x0);
                                            (*(*r22 + 0x140))(r22, r0);
                                    }
                                    if (var_5F0 != 0x0) {
                                            AComposition::GetMobID();
                                            r0 = (*(*r20 + 0x248))(r20, &var_B0, 0x0);
                                            if (r0 != 0x0) {
                                                    r0 = (*(*var_5F0 + 0x68))(var_5F0, &var_80, var_5F0[0x1] + 0x1);
                                            }
                                    }
                                    if (var_5E0 != 0x0) {
                                            r0 = (*(*var_5E0 + 0x118))(var_5E0, &var_80, AbstractMDBCompareMobID(long, void*), &var_590, 0x0);
                                            if (r0 == 0x0) {
                                                    (*(*var_5E0 + 0x68))(var_5E0, &var_80, var_590);
                                            }
                                    }
                            }
                            r23 = r27;
                            if (r25 != r27) {
                                    continue;
                            }
                            break;
                    }
            }
            else {
                    AComposition::GetMobID();
                    var_80 = var_588;
                    decomp_var_70 = var_578;
                    if (r19 != 0x0) {
                            r0 = (*(*r19 + 0x68))(r19, &var_80, r19[0x1]);
                    }
                    r0 = IsWellKnownMobID(&var_80);
                    if (r0 == 0x0 && r22 != 0x0) {
                            if (AMediaDesc::GetPhysMobType() == 0x1) {
                                    r0 = AMediaDesc::GetLocator();
                                    r28 = r0;
                                    if (r0 != 0x0) {
                                            if ((*(*r28 + 0x20))(r28, "MSMLocator") != 0x0) {
                                                    if (var_600 == 0x0) {
                                                            r0 = __dynamic_cast();
                                                            r22 = r0;
                                                            if (r0 == 0x0 && __FtlAssertFailed("msmLoc", "/Volumes/Media/Builds/workspace/MC/MCRelease_Installer/Mac_Build/coresw/ame/src/msmdb/LocalMDB.c", "AvidCoreServices.framework/Versions/A/AvidCoreServices") != 0x0) {
                                                                    DebugStr("\x11FtlAssert called.");
                                                            }
                                                            if ((*(*r22 + 0x1c0))(r22) == 0x0) {
                                                                    (*(*r22 + 0x1c8))(r22, 0x1);
                                                            }
                                                    }
                                            }
                                            else {
                                                    (**r28)(r28);
                                                    var_B0 = var_80;
                                                    r0 = MSMLocator::New(&var_B0, 0x1, 0x0);
                                                    (*(*r22 + 0x140))(r22, r0);
                                            }
                                    }
                                    else {
                                            var_B0 = var_80;
                                            r0 = MSMLocator::New(&var_B0, 0x1, 0x0);
                                            (*(*r22 + 0x140))(r22, r0);
                                    }
                                    if (var_5F0 != 0x0) {
                                            AComposition::GetMobID();
                                            r0 = (*(*r20 + 0x248))(r20, &var_B0, 0x0);
                                            if (r0 != 0x0) {
                                                    r0 = (*(*var_5F0 + 0x68))(var_5F0, &var_80, var_5F0[0x1] + 0x1);
                                            }
                                    }
                                    if (var_5E0 != 0x0) {
                                            r0 = (*(*var_5E0 + 0x118))(var_5E0, &var_80, AbstractMDBCompareMobID(long, void*), &var_590, 0x0);
                                            if (r0 == 0x0) {
                                                    (*(*var_5E0 + 0x68))(var_5E0, &var_80, var_590);
                                            }
                                    }
                            }
                    }
                    r23 = r27;
                    if (r25 != r27) {
                            continue;
                    }
                    break;
            }
    }
    goto loc_415478;
