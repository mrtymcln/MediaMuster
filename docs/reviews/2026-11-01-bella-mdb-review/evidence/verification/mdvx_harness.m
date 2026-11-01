#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#import <objc/message.h>
#include <unistd.h>

static void *field(id object, const char *name) {
    Ivar ivar = class_getInstanceVariable(object_getClass(object), name);
    return ivar ? (char *)object + ivar_getOffset(ivar) : NULL;
}
static id stringField(id object, const char *name) {
    void *p = field(object, name);
    id value = p ? *(id *)p : nil;
    return value ?: [NSNull null];
}
static uint32_t wordField(id object, const char *name) {
    void *p = field(object, name);
    return p ? *(uint32_t *)p : 0;
}
static id pairField(id object, const char *name) {
    uint32_t *p = field(object, name);
    return p ? @[@(p[0]), @(p[1])] : (id)[NSNull null];
}
static NSData *unhex(NSString *hex) {
    NSMutableData *data = [NSMutableData data];
    for (NSUInteger i = 0; i + 1 < hex.length; i += 2) {
        unsigned int value = 0;
        [[NSScanner scannerWithString:[hex substringWithRange:NSMakeRange(i, 2)]] scanHexInt:&value];
        unsigned char byte = value;
        [data appendBytes:&byte length:1];
    }
    return data;
}
static void writeResult(id result, const char *output) {
    NSData *data = [NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil];
    if (![data writeToFile:@(output) atomically:YES]) _exit(71);
}

// Runs the installed program's original MDB methods in a separately signed copy.
// The constructor exits before its application main and never changes settings.
__attribute__((constructor)) static void reviewMDB(void) {
    @autoreleasepool {
        const char *input = getenv("MDB_REVIEW_INPUT");
        const char *output = getenv("MDB_REVIEW_OUTPUT");
        if (!input || !output) return;
        NSMutableDictionary *result = [NSMutableDictionary dictionary];
        Class cls = NSClassFromString(@"MDVxOMFI");
        result[@"classAvailable"] = @(cls != Nil);
        if (cls) {
            unsigned int count = 0;
            Method *methods = class_copyMethodList(cls, &count);
            NSMutableArray *list = [NSMutableArray array];
            for (unsigned int i = 0; i < count; ++i)
                [list addObject:@{@"selector": NSStringFromSelector(method_getName(methods[i])),
                                  @"encoding": @(method_getTypeEncoding(methods[i]))}];
            free(methods);
            result[@"methods"] = list;
            id reader = [[cls alloc] init];
            NSURL *url = [NSURL fileURLWithPath:@(input)];
            SEL openSel = NSSelectorFromString(@"openFile:withIndicator:");
            BOOL opened = ((BOOL (*)(id, SEL, id, id))objc_msgSend)(reader, openSel, url, nil);
            result[@"opened"] = @(opened);
            SEL countSel = NSSelectorFromString(@"getMDBCompositionsCount");
            result[@"compositionCount"] = @(((uint64_t (*)(id, SEL))objc_msgSend)(reader, countSel));
            result[@"stage"] = @"opened";
            writeResult(result, output);
            const char *queriesPath = getenv("MDB_REVIEW_QUERIES");
            NSArray *queries = queriesPath ? [NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:@(queriesPath)] options:0 error:nil] : @[];
            NSMutableArray *rows = [NSMutableArray array];
            result[@"rows"] = rows;
            if (opened) for (NSDictionary *query in queries) {
                @try {
                    id item = [[NSClassFromString(@"MDVxDatabaseItem") alloc] init];
                    NSData *file = unhex(query[@"fileHex"]);
                    NSData *master = unhex(query[@"masterHex"]);
                    if (file.length == 32) memcpy(field(item, "filePackageUID"), file.bytes, 32);
                    if (master.length == 32) memcpy(field(item, "materialPackageUID"), master.bytes, 32);
                    ((void (*)(id, SEL, id))objc_msgSend)(reader, NSSelectorFromString(@"addInfoToItem:"), item);
                    unsigned char *cc = ((void *(*)(id, SEL))objc_msgSend)(item, NSSelectorFromString(@"DB_CompressionFourCC"));
                    NSString *ccHex = [NSString stringWithFormat:@"%02x%02x%02x%02x",cc[0],cc[1],cc[2],cc[3]];
                    [rows addObject:@{@"query":query,
                                      @"nameInBin":stringField(item, "_DB_NameInBin"),
                                      @"project":stringField(item, "_DB_ProjectName"),
                                      @"clipName":stringField(item, "_DB_ClipName"),
                                      @"usageCode":@(wordField(item, "_DB_usageCode")),
                                      @"resolutionId":@(wordField(item, "_DB_DIDResolutionID")),
                                      @"bits":@(wordField(item, "_DB_bits")),
                                      @"frameRate":pairField(item, "_DB_framerate"),
                                      @"sampleRate":pairField(item, "_DB_samplerate"),
                                      @"compressionFourCCHex":ccHex}];
                    [item release];
                } @catch (NSException *exception) {
                    [rows addObject:@{@"query":query,@"exception":exception.description}];
                }
            }
            result[@"stage"] = @"complete";
            [reader release];
        }
        writeResult(result, output);
        _exit(0);
    }
}
