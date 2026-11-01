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
    r0 = ___chkstk_darwin_1210d80(arg0, arg1, arg2, arg3);
    r31 = r31 + 0xffffffffffffff90 - "x" - 0x9a0;
    r21 = r4;
    var_9C1 = r3;
    r19 = r2;
    r23 = r1;
    var_9B1 = r0;
    var_70 = **___stack_chk_guard;
    var_89 = 0x0;
    var_8D = 0x0;
    var_C1 = q0;
    VolInfo_t::VolInfo_t();
    OSVolID::OSVolID();
    if (r23[0x1] != (r19[0x1] - *r19) / 0x4 && __FtlAssertFailed("inDirectories->GetNumItems() == flags.size()", "/Volumes/Media/Builds/workspace/MC/MCRelease_Installer/Mac_Build/coresw/ame/src/msmdb/LocalMDB.c", 0xd0) != 0x0) {
            DebugStr("\x11FtlAssert called.");
    }
    var_9D1 = time(0x0);
    bzero(&arg_37B, "d.72.1.dylib");
    r0 = mach_timebase_info(&var_79);
    r20 = mach_absolute_time();
    r24 = var_79;
    r22 = var_75;
    if (r21 != 0x0) {
            r10 = "TRUE";
    }
    else {
            r10 = "FALSE";
    }
    r1 = "d.72.1.dylib";
    dprintf1("LocalMDB::LoadMobsFromMDB() inCallback=%p sourceOnle=%s\n");
    dprintf1("    mediaDirPath phyMobs logMobs\n");
    r8 = r20 * r24 / r22 >> 0x3;
    asm { umulh      x8, x8, x9 };
    var_9C9 = r8 >> 0x4;
    if (var_9C1 == 0x0) goto loc_413cc8;
    goto loc_413878;

loc_413cc8:
    var_9A9 = 0x0;
    goto loc_413ccc;

loc_413878:
    r22 = *r19;
    r1 = &decomp_var_981;
    r26 = (*(*r23 + 0x238))(r23, r1, 0x0);
    r27 = 0x0;
    r28 = 0x0;
    var_9A9 = 0x0;
    r24 = &var_949;
    goto loc_4138b0;

loc_413ccc:
    if (var_9B1[0x1] != 0x0) {
            asm { sxtw       x1, w8 };
            var_9B9 = AArray::NewWithSlotAndBlock(0x20, r1, 0x64);
    }
    else {
            var_9B9 = 0x0;
    }
    XError::XError();
    r27 = *r19;
    r0 = (*(*r23 + 0x238))(r23, &decomp_var_959, 0x0);
    r20 = r0;
    r25 = 0x0;
    r22 = 0x0;
    d8 = 1.0e+06;
    goto loc_413d34;

loc_4138b0:
    if (r26 != 0x0) {
            XError::XError();
            arg_37B = 0x0;
            r0 = (*(*r26 + 0x1a0))(r26, &arg_37B, 0x401, 0x0);
            r8 = *(int32_t *)r22;
            if (r8 != 0x0) {
                    r27 = 0x1;
            }
            if (r8 != 0x0) {
                    r26 = (*(*GetMSMDB() + 0x178))();
            }
            r0 = MSMOpenStreamMDB(r26, 0x1, r27, 0x0, &var_89);
            r0 = var_89;
            if (r0 != 0x0) {
                    r26 = omfiHPDomain::New(r0, 0x0, 0x1, 0x0);
                    if (var_89 != 0x0 && r26 != 0x0) {
                            (*(*r26 + 0x268))(r26, 0x2);
                            r0 = (*(*r26 + 0x240))(r26, 0x2);
                            r28 = r0;
                            var_9A9 = var_9A9 + r0;
                            if (r21 != 0x0) {
                                    r27 = 0x0;
                            }
                            else {
                                    (*(*r26 + 0x268))(r26, 0x1);
                                    r0 = (*(*r26 + 0x240))(r26, 0x1);
                                    var_9A9 = var_9A9 + r0;
                                    r27 = r0;
                            }
                    }
                    else {
                            r27 = 0x0;
                            r28 = 0x0;
                    }
                    r25 = r26;
            }
            else {
                    r27 = 0x0;
                    r28 = 0x0;
                    r25 = 0x0;
            }
            r0 = StSetCurrentException::StSetCurrentException(&var_9A1);
            UTF16ToCurrentMacMBCSEncodingString(&arg_37B, 0x0);
            r9 = var_949;
            if (sign_extend_64(var_932) >= 0x0) {
                    r8 = r24;
            }
            else {
                    r8 = r9;
            }
            dprintf1("    \"%s\" %d %d\n");
            if (r25 != 0x0) {
                    (**r25)(r25);
            }
            MSMCloseStreamMDB(0x0, &var_89);
            var_89 = 0x0;
            if ((sign_extend_64(var_932) & 0xffffffff80000000) != 0x0) {
                    operator delete(var_949);
            }
            r22 = r22 + 0x4;
            StSetCurrentException::~StSetCurrentException();
            XError::~XError();
            r1 = &decomp_var_981;
            r26 = (*(*r23 + 0x240))(r23, r1, 0x0);
            continue;
    }
    break;

loc_413d34:
    if (r20 != 0x0) {
            XError::XError();
            r0 = mach_timebase_info(&var_81);
            r8 = mach_absolute_time() * var_81 / var_7D >> 0x3;
            asm { umulh      x8, x8, x9 };
            r24 = r8 >> 0x4;
            arg_37B = 0x0;
            r0 = (*(*r20 + 0x1a0))(r20, &arg_37B, 0x401, 0x0);
            var_A1 = 0x0;
            var_B9 = 0x0;
            r8 = *(int32_t *)r27;
            if (r8 != 0x0) {
                    r19 = 0x1;
            }
            r0 = r20;
            if (r8 != 0x0) {
                    r0 = (*(*GetMSMDB() + 0x178))();
            }
            r0 = MSMOpenStreamMDB(r0, 0x1, r19, 0x0, &var_89);
            if (var_89 == 0x0) {
                    r0 = StSetCurrentException::StSetCurrentException(&decomp_var_989);
                    r0 = MSMCloseStreamMDB(0x0, &var_89);
                    r0 = mach_timebase_info(&var_9A1);
                    r8 = mach_absolute_time() * var_9A1 / var_9A1 >> 0x3;
                    asm { umulh      x8, x8, x9 };
                    r25 = r8 >> 0x4;
                    UTF16ToCurrentMacMBCSEncodingString(&arg_37B, 0x0);
                    r9 = var_9A1;
                    r8 = &var_9A1;
                    if (sign_extend_64(var_98A) < 0x0) {
                            r8 = r9;
                    }
                    dprintf1("    \"%s\" %.2f sec\n");
                    if (var_971 == 0x0) {
                            if ((sign_extend_64(var_98A) & 0xffffffff80000000) != 0x0) {
                                    operator delete(var_9A1);
                            }
                            StSetCurrentException::~StSetCurrentException();
                            XError::~XError();
                            if ((r22 & 0xff) != 0x0) {
                                    XError::XError(&var_79);
                                    r0 = operator+(&var_79);
                                    XError::~XError();
                            }
                            r27 = r27 + 0x4;
                            r0 = (*(*r23 + 0x240))(r23, &decomp_var_959, 0x0);
                            r20 = r0;
                            continue;
                    }
                    r0 = XError::FindCode(&decomp_var_981);
                    if (r0 != 0x0) {
                            if (var_971 != 0x0) {
                                    r0 = xraisexerror(&decomp_var_981);
                            }
                    }
                    else {
                            stx_func("An error occurred loading media database: %s.\n");
                            sprintf(&var_79, AvText::operator char const*());
                            r0 = strlen(&var_79);
                            (*(*r20 + 0x198))(r20, &var_79 + r0, 0x400 - r0, 0x0);
                            (*(*r20 + 0x198))(r20, &arg_5ef + 0x58f, 0x401, 0x0);
                            dprintf1("Failure occurred loading .mdb from dir \"%s\"\n");
                            stx_func("OK");
                            r0 = AvText::operator char const*();
                            r0 = UIdialog(&var_79, r0, 0x1, 0x0, 0x0, 0x1, "/Volumes/Media/Builds/workspace/MC/MCRelease_Installer/Mac_Build/coresw/ame/src/msmdb/LocalMDB.c");
                            r0 = XError::FindCode(&decomp_var_981);
                            if (r0 != 0x0) {
                                    r22 = 0x1;
                            }
                    }
                    if ((sign_extend_64(var_98A) & 0xffffffff80000000) != 0x0) {
                            operator delete(var_9A1);
                    }
                    StSetCurrentException::~StSetCurrentException();
                    XError::~XError();
                    if ((r22 & 0xff) != 0x0) {
                            XError::XError(&var_79);
                            r0 = operator+(&var_79);
                            XError::~XError();
                    }
                    r27 = r27 + 0x4;
                    r0 = (*(*r23 + 0x240))(r23, &decomp_var_959, 0x0);
                    r20 = r0;
                    continue;
            }
            r0 = AArray::GetBadIndexValue();
            if (r21 != 0x0) {
                    r19 = r0;
                    if (var_B9 != 0x0) {
                            r8 = dbgTestFlag(*(int32_t *)AMediaStreamMgr::MSMDbgID, 0x2);
                            r0 = var_B9;
                            if (r8 != 0x0) {
                                    (*(*r0 + 0x198))();
                                    dprintf1("  DEBUG(msmdb): LoadMobsFromMDB loading .mdb from dir: %s\n");
                                    r0 = var_B9;
                            }
                            r8 = var_89;
                            r0 = LocalMDB::ScanOMFFileToMobManager(r8, var_9A9, &var_8D, var_9C1, r21, 0x0, 0x0, 0x0, var_A1, var_9B9, r0, 0x0);
                            if (r21 == 0x0) {
                                    r26 = r0;
                                    if (r19 == AArray::GetBadIndexValue()) {
                                            if (__FtlAssertFailed("dirIndex != BAD_INDEX", "/Volumes/Media/Builds/workspace/MC/MCRelease_Installer/Mac_Build/coresw/ame/src/msmdb/LocalMDB.c", 0x17e) != 0x0) {
                                                    DebugStr("\x11FtlAssert called.");
                                                    if (r26 != 0x0) {
                                                            var_C1 = r8;
                                                            r0 = var_9B1[0x2];
                                                            r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                    }
                                                    else {
                                                            var_C1 = 0x1;
                                                            r0 = var_9B1[0x2];
                                                            r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                    }
                                            }
                                            else {
                                                    if (r26 != 0x0) {
                                                            var_C1 = r8;
                                                            r0 = var_9B1[0x2];
                                                            r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                    }
                                                    else {
                                                            var_C1 = 0x1;
                                                            r0 = var_9B1[0x2];
                                                            r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                    }
                                            }
                                    }
                                    else {
                                            if (r26 != 0x0) {
                                                    var_C1 = r8;
                                                    r0 = var_9B1[0x2];
                                                    r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                            }
                                            else {
                                                    var_C1 = 0x1;
                                                    r0 = var_9B1[0x2];
                                                    r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                            }
                                    }
                            }
                    }
                    else {
                            r8 = var_89;
                            r0 = LocalMDB::ScanOMFFileToMobManager(r8, var_9A9, &var_8D, var_9C1, r21, 0x0, 0x0, 0x0, var_A1, var_9B9, 0x0, 0x0);
                            if (r21 == 0x0) {
                                    r26 = r0;
                                    if (r19 == AArray::GetBadIndexValue()) {
                                            if (__FtlAssertFailed("dirIndex != BAD_INDEX", "/Volumes/Media/Builds/workspace/MC/MCRelease_Installer/Mac_Build/coresw/ame/src/msmdb/LocalMDB.c", 0x17e) != 0x0) {
                                                    DebugStr("\x11FtlAssert called.");
                                                    if (r26 != 0x0) {
                                                            var_C1 = r8;
                                                            r0 = var_9B1[0x2];
                                                            r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                    }
                                                    else {
                                                            var_C1 = 0x1;
                                                            r0 = var_9B1[0x2];
                                                            r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                    }
                                            }
                                            else {
                                                    if (r26 != 0x0) {
                                                            var_C1 = r8;
                                                            r0 = var_9B1[0x2];
                                                            r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                    }
                                                    else {
                                                            var_C1 = 0x1;
                                                            r0 = var_9B1[0x2];
                                                            r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                    }
                                            }
                                    }
                                    else {
                                            if (r26 != 0x0) {
                                                    var_C1 = r8;
                                                    r0 = var_9B1[0x2];
                                                    r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                            }
                                            else {
                                                    var_C1 = 0x1;
                                                    r0 = var_9B1[0x2];
                                                    r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                            }
                                    }
                            }
                    }
            }
            else {
                    var_B9 = r20;
                    r0 = var_9B1[0x2];
                    r0 = (*(*r0 + 0xb0))(r0, &var_C1, LocalMDB::FindByDirectory(void*, void*));
                    r19 = r0;
                    if (r19 == AArray::GetBadIndexValue()) {
                            var_C1 = 0x0;
                            (*(*AArray::NewWithSlotAndBlock(0x28, 0x100, 0x100) + 0xc0))();
                            (*(*AArray::NewWithSlotAndBlock(0x28, 0x100, 0x100) + 0xc0))();
                            var_A1 = AArray::NewWithSlotAndBlock(0x20, "vid.72.1.dylib", "vid.72.1.dylib");
                            r0 = var_9B1[0x2];
                            r8 = *r0;
                            r19 = r0[0x1];
                            r1 = &var_C1;
                            (r8[0xd])(r0, r1, r19);
                    }
                    else {
                            r0 = var_9B1[0x2];
                            r1 = &var_C1;
                            (*(*r0 + 0x90))(r0, r1, r19);
                    }
                    r28 = r28 & 0xffffffff00000000 | r1;
                    AVolumeInfo((*(*var_B9 + 0x1d0))(var_B9), r28);
                    if (var_C1 != 0x0) {
                            if (var_F1 == 0x0) {
                                    if (*(int8_t *)Ame_gMediaToolFull() == 0x0) {
                                            r0 = StSetCurrentException::StSetCurrentException(&decomp_var_989);
                                            r0 = MSMCloseStreamMDB(0x0, &var_89);
                                            r0 = mach_timebase_info(&var_9A1);
                                            r8 = mach_absolute_time() * var_9A1 / var_9A1 >> 0x3;
                                            asm { umulh      x8, x8, x9 };
                                            r25 = r8 >> 0x4;
                                            UTF16ToCurrentMacMBCSEncodingString(&arg_37B, 0x0);
                                            r9 = var_9A1;
                                            r8 = &var_9A1;
                                            if (sign_extend_64(var_98A) < 0x0) {
                                                    r8 = r9;
                                            }
                                            dprintf1("    \"%s\" %.2f sec\n");
                                            if (var_971 == 0x0) {
                                                    if ((sign_extend_64(var_98A) & 0xffffffff80000000) != 0x0) {
                                                            operator delete(var_9A1);
                                                    }
                                                    StSetCurrentException::~StSetCurrentException();
                                                    XError::~XError();
                                                    if ((r22 & 0xff) != 0x0) {
                                                            XError::XError(&var_79);
                                                            r0 = operator+(&var_79);
                                                            XError::~XError();
                                                    }
                                                    r27 = r27 + 0x4;
                                                    r0 = (*(*r23 + 0x240))(r23, &decomp_var_959, 0x0);
                                                    r20 = r0;
                                                    continue;
                                            }
                                            r0 = XError::FindCode(&decomp_var_981);
                                            if (r0 != 0x0) {
                                                    if (var_971 != 0x0) {
                                                            r0 = xraisexerror(&decomp_var_981);
                                                    }
                                            }
                                            else {
                                                    stx_func("An error occurred loading media database: %s.\n");
                                                    sprintf(&var_79, AvText::operator char const*());
                                                    r0 = strlen(&var_79);
                                                    (*(*r20 + 0x198))(r20, &var_79 + r0, 0x400 - r0, 0x0);
                                                    (*(*r20 + 0x198))(r20, &arg_5ef + 0x58f, 0x401, 0x0);
                                                    dprintf1("Failure occurred loading .mdb from dir \"%s\"\n");
                                                    stx_func("OK");
                                                    r0 = AvText::operator char const*();
                                                    r0 = UIdialog(&var_79, r0, 0x1, 0x0, 0x0, 0x1, "/Volumes/Media/Builds/workspace/MC/MCRelease_Installer/Mac_Build/coresw/ame/src/msmdb/LocalMDB.c");
                                                    r0 = XError::FindCode(&decomp_var_981);
                                                    if (r0 != 0x0) {
                                                            r22 = 0x1;
                                                    }
                                            }
                                            if ((sign_extend_64(var_98A) & 0xffffffff80000000) != 0x0) {
                                                    operator delete(var_9A1);
                                            }
                                            StSetCurrentException::~StSetCurrentException();
                                            XError::~XError();
                                            if ((r22 & 0xff) != 0x0) {
                                                    XError::XError(&var_79);
                                                    r0 = operator+(&var_79);
                                                    XError::~XError();
                                            }
                                            r27 = r27 + 0x4;
                                            r0 = (*(*r23 + 0x240))(r23, &decomp_var_959, 0x0);
                                            r20 = r0;
                                            continue;
                                    }
                                    if (var_B9 != 0x0) {
                                            r8 = dbgTestFlag(*(int32_t *)AMediaStreamMgr::MSMDbgID, 0x2);
                                            r0 = var_B9;
                                            if (r8 != 0x0) {
                                                    (*(*r0 + 0x198))();
                                                    dprintf1("  DEBUG(msmdb): LoadMobsFromMDB loading .mdb from dir: %s\n");
                                                    r0 = var_B9;
                                            }
                                    }
                                    else {
                                            r0 = 0x0;
                                    }
                                    r8 = var_89;
                                    r0 = LocalMDB::ScanOMFFileToMobManager(r8, var_9A9, &var_8D, var_9C1, r21, 0x0, 0x0, 0x0, var_A1, var_9B9, r0, 0x0);
                                    if (r21 == 0x0) {
                                            r26 = r0;
                                            if (r19 == AArray::GetBadIndexValue() && __FtlAssertFailed("dirIndex != BAD_INDEX", "/Volumes/Media/Builds/workspace/MC/MCRelease_Installer/Mac_Build/coresw/ame/src/msmdb/LocalMDB.c", 0x17e) != 0x0) {
                                                    DebugStr("\x11FtlAssert called.");
                                            }
                                            if (r26 == 0x0) {
                                                    r8 = 0x1;
                                            }
                                            var_C1 = r8;
                                            r0 = var_9B1[0x2];
                                            r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                    }
                            }
                            else {
                                    if (var_B9 != 0x0) {
                                            r8 = dbgTestFlag(*(int32_t *)AMediaStreamMgr::MSMDbgID, 0x2);
                                            r0 = var_B9;
                                            if (r8 != 0x0) {
                                                    (*(*r0 + 0x198))();
                                                    dprintf1("  DEBUG(msmdb): LoadMobsFromMDB loading .mdb from dir: %s\n");
                                                    r0 = var_B9;
                                            }
                                            r8 = var_89;
                                            r0 = LocalMDB::ScanOMFFileToMobManager(r8, var_9A9, &var_8D, var_9C1, r21, 0x0, 0x0, 0x0, var_A1, var_9B9, r0, 0x0);
                                            if (r21 == 0x0) {
                                                    r26 = r0;
                                                    if (r19 == AArray::GetBadIndexValue()) {
                                                            if (__FtlAssertFailed("dirIndex != BAD_INDEX", "/Volumes/Media/Builds/workspace/MC/MCRelease_Installer/Mac_Build/coresw/ame/src/msmdb/LocalMDB.c", 0x17e) != 0x0) {
                                                                    DebugStr("\x11FtlAssert called.");
                                                                    if (r26 != 0x0) {
                                                                            var_C1 = r8;
                                                                            r0 = var_9B1[0x2];
                                                                            r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                                    }
                                                                    else {
                                                                            var_C1 = 0x1;
                                                                            r0 = var_9B1[0x2];
                                                                            r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                                    }
                                                            }
                                                            else {
                                                                    if (r26 == 0x0) {
                                                                            r8 = 0x1;
                                                                    }
                                                                    var_C1 = r8;
                                                                    r0 = var_9B1[0x2];
                                                                    r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                            }
                                                    }
                                                    else {
                                                            if (r26 == 0x0) {
                                                                    r8 = 0x1;
                                                            }
                                                            var_C1 = r8;
                                                            r0 = var_9B1[0x2];
                                                            r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                    }
                                            }
                                    }
                                    else {
                                            r8 = var_89;
                                            r0 = LocalMDB::ScanOMFFileToMobManager(r8, var_9A9, &var_8D, var_9C1, r21, 0x0, 0x0, 0x0, var_A1, var_9B9, 0x0, 0x0);
                                            if (r21 == 0x0) {
                                                    r26 = r0;
                                                    if (r19 == AArray::GetBadIndexValue() && __FtlAssertFailed("dirIndex != BAD_INDEX", "/Volumes/Media/Builds/workspace/MC/MCRelease_Installer/Mac_Build/coresw/ame/src/msmdb/LocalMDB.c", 0x17e) != 0x0) {
                                                            DebugStr("\x11FtlAssert called.");
                                                    }
                                                    if (r26 == 0x0) {
                                                            r8 = 0x1;
                                                    }
                                                    var_C1 = r8;
                                                    r0 = var_9B1[0x2];
                                                    r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                            }
                                    }
                            }
                    }
                    else {
                            if (var_B9 != 0x0) {
                                    r8 = dbgTestFlag(*(int32_t *)AMediaStreamMgr::MSMDbgID, 0x2);
                                    r0 = var_B9;
                                    if (r8 != 0x0) {
                                            (*(*r0 + 0x198))();
                                            dprintf1("  DEBUG(msmdb): LoadMobsFromMDB loading .mdb from dir: %s\n");
                                            r0 = var_B9;
                                    }
                                    r8 = var_89;
                                    r0 = LocalMDB::ScanOMFFileToMobManager(r8, var_9A9, &var_8D, var_9C1, r21, 0x0, 0x0, 0x0, var_A1, var_9B9, r0, 0x0);
                                    if (r21 == 0x0) {
                                            r26 = r0;
                                            if (r19 == AArray::GetBadIndexValue()) {
                                                    if (__FtlAssertFailed("dirIndex != BAD_INDEX", "/Volumes/Media/Builds/workspace/MC/MCRelease_Installer/Mac_Build/coresw/ame/src/msmdb/LocalMDB.c", 0x17e) != 0x0) {
                                                            DebugStr("\x11FtlAssert called.");
                                                            if (r26 != 0x0) {
                                                                    var_C1 = r8;
                                                                    r0 = var_9B1[0x2];
                                                                    r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                            }
                                                            else {
                                                                    var_C1 = 0x1;
                                                                    r0 = var_9B1[0x2];
                                                                    r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                            }
                                                    }
                                                    else {
                                                            if (r26 != 0x0) {
                                                                    var_C1 = r8;
                                                                    r0 = var_9B1[0x2];
                                                                    r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                            }
                                                            else {
                                                                    var_C1 = 0x1;
                                                                    r0 = var_9B1[0x2];
                                                                    r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                            }
                                                    }
                                            }
                                            else {
                                                    if (r26 != 0x0) {
                                                            var_C1 = r8;
                                                            r0 = var_9B1[0x2];
                                                            r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                    }
                                                    else {
                                                            var_C1 = 0x1;
                                                            r0 = var_9B1[0x2];
                                                            r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                    }
                                            }
                                    }
                            }
                            else {
                                    r8 = var_89;
                                    r0 = LocalMDB::ScanOMFFileToMobManager(r8, var_9A9, &var_8D, var_9C1, r21, 0x0, 0x0, 0x0, var_A1, var_9B9, 0x0, 0x0);
                                    if (r21 == 0x0) {
                                            r26 = r0;
                                            if (r19 == AArray::GetBadIndexValue()) {
                                                    if (__FtlAssertFailed("dirIndex != BAD_INDEX", "/Volumes/Media/Builds/workspace/MC/MCRelease_Installer/Mac_Build/coresw/ame/src/msmdb/LocalMDB.c", 0x17e) != 0x0) {
                                                            DebugStr("\x11FtlAssert called.");
                                                            if (r26 != 0x0) {
                                                                    var_C1 = r8;
                                                                    r0 = var_9B1[0x2];
                                                                    r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                            }
                                                            else {
                                                                    var_C1 = 0x1;
                                                                    r0 = var_9B1[0x2];
                                                                    r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                            }
                                                    }
                                                    else {
                                                            if (r26 == 0x0) {
                                                                    r8 = 0x1;
                                                            }
                                                            var_C1 = r8;
                                                            r0 = var_9B1[0x2];
                                                            r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                                    }
                                            }
                                            else {
                                                    if (r26 == 0x0) {
                                                            r8 = 0x1;
                                                    }
                                                    var_C1 = r8;
                                                    r0 = var_9B1[0x2];
                                                    r0 = (*(*r0 + 0x88))(r0, &var_C1, r19);
                                            }
                                    }
                            }
                    }
            }
            r0 = StSetCurrentException::StSetCurrentException(&decomp_var_989);
            r0 = MSMCloseStreamMDB(0x0, &var_89);
            r0 = mach_timebase_info(&var_9A1);
            r8 = mach_absolute_time() * var_9A1 / var_9A1 >> 0x3;
            asm { umulh      x8, x8, x9 };
            r25 = r8 >> 0x4;
            UTF16ToCurrentMacMBCSEncodingString(&arg_37B, 0x0);
            r9 = var_9A1;
            r8 = &var_9A1;
            if (sign_extend_64(var_98A) < 0x0) {
                    r8 = r9;
            }
            dprintf1("    \"%s\" %.2f sec\n");
            if (var_971 == 0x0) {
                    if ((sign_extend_64(var_98A) & 0xffffffff80000000) != 0x0) {
                            operator delete(var_9A1);
                    }
                    StSetCurrentException::~StSetCurrentException();
                    XError::~XError();
                    if ((r22 & 0xff) != 0x0) {
                            XError::XError(&var_79);
                            r0 = operator+(&var_79);
                            XError::~XError();
                    }
                    r27 = r27 + 0x4;
                    r0 = (*(*r23 + 0x240))(r23, &decomp_var_959, 0x0);
                    r20 = r0;
                    continue;
            }
            r0 = XError::FindCode(&decomp_var_981);
            if (r0 != 0x0) {
                    if (var_971 != 0x0) {
                            r0 = xraisexerror(&decomp_var_981);
                    }
            }
            else {
                    stx_func("An error occurred loading media database: %s.\n");
                    sprintf(&var_79, AvText::operator char const*());
                    r0 = strlen(&var_79);
                    (*(*r20 + 0x198))(r20, &var_79 + r0, 0x400 - r0, 0x0);
                    (*(*r20 + 0x198))(r20, &arg_5ef + 0x58f, 0x401, 0x0);
                    dprintf1("Failure occurred loading .mdb from dir \"%s\"\n");
                    stx_func("OK");
                    r0 = AvText::operator char const*();
                    r0 = UIdialog(&var_79, r0, 0x1, 0x0, 0x0, 0x1, "/Volumes/Media/Builds/workspace/MC/MCRelease_Installer/Mac_Build/coresw/ame/src/msmdb/LocalMDB.c");
                    r0 = XError::FindCode(&decomp_var_981);
                    if (r0 != 0x0) {
                            r22 = 0x1;
                    }
            }
            if ((sign_extend_64(var_98A) & 0xffffffff80000000) != 0x0) {
                    operator delete(var_9A1);
            }
            StSetCurrentException::~StSetCurrentException();
            XError::~XError();
            if ((r22 & 0xff) != 0x0) {
                    XError::XError(&var_79);
                    r0 = operator+(&var_79);
                    XError::~XError();
            }
            r27 = r27 + 0x4;
            r0 = (*(*r23 + 0x240))(r23, &decomp_var_959, 0x0);
            r20 = r0;
            continue;
    }
    break;
    r0 = mach_timebase_info(&var_79);
    mach_absolute_time() * var_79 / var_75 >> 0x3;
    asm { umulh      x8, x8, x9 };
    dprintf1("    total_num_mobs %d\n    First pass %.2f sec\n\n");
    goto loc_413ccc;
    r0 = StSetCurrentException::StSetCurrentException(&var_79);
    d0 = (r25 - var_9C9) / 1.0e+06;
    r20 = var_8D;
    dprintf1("    total time=%.2f mobs=%d\n");
    if (var_9B9 != 0x0) {
            r8 = var_9B1[0x1];
            if (r8 != 0x0 && var_9B9[0x1] >= 0x1) {
                    (r8)(var_9B9);
            }
            (**var_9B9)(var_9B9);
    }
    if (var_939 != 0x0) {
            r0 = xraisexerror(&var_949);
    }
    StSetCurrentException::~StSetCurrentException();
    XError::~XError();
    r19 = time(0x0);
    if (dbgTestFlag(*(int32_t *)AMediaStreamMgr::MSMDbgID, 0x2) != 0x0) {
            difftime(r19, var_9D1);
            dprintf1("DEBUG(msmdb): LoadMobsFromMDB loaded %d mobs (for volume, all sub-dirs) took %6.1f secs\n");
    }
    r0 = VolInfo_t::~VolInfo_t();
    if (**___stack_chk_guard != var_70) {
            __stack_chk_fail();
    }
    return r0;
