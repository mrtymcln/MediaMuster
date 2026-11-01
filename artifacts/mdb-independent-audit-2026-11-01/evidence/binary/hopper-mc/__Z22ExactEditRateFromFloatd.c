    asm { fcvtzs     w8, d0 };
    if (r8 >= d0) goto loc_25771c;
    goto loc_257690;

loc_25771c:
    r9 = 0x0;
    if (r8 == 0x0) {
            r0 = 0x0 | (0x0 & 0xffff) * 0x1;
    }
    else {
            r11 = 0x19999998;
            r12 = 0xcccccccd + r12 * r8;
            asm { ror        w12, w12, #0x1 };
            if (r12 <= r11) {
                    do {
                            r10 = 0xcccccccd;
                            r13 = 0x19999999;
                            asm { smull      x8, w8, w12 };
                            r14 = r8 >> 0x3f;
                            r8 = (SAR(r8, 0x22)) + r14;
                            r9 = r9 + 0x1;
                            r14 = r10 + r14 * r8;
                            asm { ror        w14, w14, #0x1 };
                    } while (r14 < r13);
            }
            r0 = r8 | (r9 & 0xffff) * 0x1;
    }
    return r0;

loc_257690:
    r9 = 0xffffffff;
    d1 = 0x4024000000000000;
    goto loc_257698;

loc_257698:
    r9 = r9 + 0x1;
    d0 = d0 * d1;
    asm { fcvtzs     w8, d0 };
    if (r9 <= 0x2 && d0 > r8) {
            continue;
    }
    break;
    r9 = r9 ^ 0xffffffff;
    if (r8 != 0x0) {
            r11 = 0x19999998;
            r12 = 0xcccccccd + r12 * r8;
            asm { ror        w12, w12, #0x1 };
            if (r12 <= r11) {
                    do {
                            r10 = 0xcccccccd;
                            r13 = 0x19999999;
                            asm { smull      x8, w8, w12 };
                            r14 = r8 >> 0x3f;
                            r8 = (SAR(r8, 0x22)) + r14;
                            r9 = r9 + 0x1;
                            r14 = r10 + r14 * r8;
                            asm { ror        w14, w14, #0x1 };
                    } while (r14 < r13);
            }
            r0 = r8 | (r9 & 0xffff) * 0x1;
    }
    else {
            r0 = 0x0 | (r9 & 0xffff) * 0x1;
    }
    return r0;
