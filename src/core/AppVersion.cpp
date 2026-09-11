#include "AppVersion.hpp"

#ifdef __APPLE__
#import <Foundation/Foundation.h>
#endif

namespace LinguaAlpaca {

std::string GetAppVersion() {
#ifdef __APPLE__
    @autoreleasepool {
        NSBundle* bundle = [NSBundle mainBundle];
        if (bundle) {
            NSString* ver = [bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
            if (ver && [ver length] > 0) {
                return [ver UTF8String];
            }
        }
    }
#endif

#ifdef APP_VERSION
    return APP_VERSION;
#else
    return "1.0.0";
#endif
}

} // namespace LinguaAlpaca
