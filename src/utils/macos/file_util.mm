#include <iostream>
#import <Foundation/Foundation.h>
#include "pag/defines.h"

namespace pag {

PAG_API std::string getPlatformTemporaryDirectory() {
    NSArray *docPaths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString *documentsDirectory = [docPaths firstObject];
    
    //NSString *tempDir = NSTemporaryDirectory();

    // 转换为 std::string
    return std::string([documentsDirectory UTF8String]);
}


}  // namespace pag
