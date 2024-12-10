#pragma once

//zzy
namespace pag {

// 通用错误码
enum ErrorCode {
    SUCCESS = 0,          // 操作成功
    UNKNOWN_ERROR = -1,   // 未知错误
    INVALID_ARGUMENT = -2, // 参数错误

    AGAIN = -5,           // 不用解释
    SOURCE_DRAINS = -6,   // 源数据耗尽
};


}
