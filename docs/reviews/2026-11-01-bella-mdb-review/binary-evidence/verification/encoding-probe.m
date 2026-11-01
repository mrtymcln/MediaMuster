#import <Foundation/Foundation.h>
int main(void) { @autoreleasepool { unsigned char b[]={0x80,0x8e,0xe9}; for (NSNumber *e in @[@1,@5,@30]) {NSString *s=[[NSString alloc] initWithBytes:b length:3 encoding:e.unsignedIntegerValue]; printf("encoding %lu => %s\n",e.unsignedLongValue,s.UTF8String?:"nil");} } }
