#ifndef API_ERROR_CODES_H
#define API_ERROR_CODES_H

/* 
 * 定义 X-Macro (宏是预处理器指令，不受命名空间影响，但我们将其放在这里方便管理)
 * 格式: X(EnumName, Value, "Error Message")
 */
#define API_ERROR_CODE_MAP(X) \
    X(ApiError_Success, 0, "Success") \
    /* --- General Errors (-1xx) --- */ \
    X(ApiError_InvalidJsonFormat, -101, "请求体必须是JSON格式 ") \
    X(ApiError_MissingRequiredArgs, -102, "缺少必要参数 ") \
    X(ApiError_InternalError, -103, "内部错误 ") \
    X(ApiError_DatabaseError, -104, "数据库错误 ") \
    X(ApiError_InvalidOperation, -105, "无效操作 ") \
    X(ApiError_TooManyRequests, -106, "访问过于频繁，请稍后再试 ") \
    /* --- Authentication & Token Errors (-2xx) --- */ \
    X(ApiError_TokenInvalidOrExpired, -201, "Token已失效 ") \
    X(ApiError_InvalidTokenType, -202, "Token类型错误 ") \
    X(ApiError_TokenMissing, -203, "缺少Token ") \
    X(ApiError_FlashTokenInvalidType, -204, "不是FlashToken ") \
    X(ApiError_FlashTokenVerificationFailed, -205, "FlashToken验证失败 ") \
    X(ApiError_FlashTokenExpired, -206, "FlashToken已失效 ") \
    X(ApiError_AuthenticationFailed, -207, "验证失败 ") \
    X(ApiError_ActionTokenInvalid, -208, "ActionToken 无效、已过期或无权执行此操作 ") \
    X(ApiError_TokenTypeUnexpected, -209, "Token类型不符 ") \
    X(ApiError_TokenTypeInvalid, -210, "未知Token类型 ") \
    X(ApiError_FlashTokenInvalidOrExpired, -211, "不是有效的 FlashToken / FlashToken已失效 ") \
    /* --- User Account Errors (-3xx) --- */ \
    X(ApiError_InvalidCredentials, -301, "用户名或密码错误 ") \
    X(ApiError_UserNotFound, -302, "用户不存在 ") \
    X(ApiError_UsernameAlreadyExists, -303, "用户名已存在 ") \
    X(ApiError_EmailAlreadyExists, -304, "邮箱已存在 ") \
    X(ApiError_PhoneAlreadyExists, -305, "手机号已存在 ") \
    X(ApiError_InvalidUsernameFormat, -306, "用户名格式不合法 ") \
    X(ApiError_PasswordNotSet, -307, "用户没有设置密码 ") \
    X(ApiError_UserUpdateFailed, -308, "更新失败 ") \
    X(ApiError_UserCreationFailure, -309, "创建用户失败 ") \
    X(ApiError_UserDeletionFailure, -310, "删除失败 ") \
    X(ApiError_ConcurrentModificationError, -311, "电话或邮箱不能同时修改 ") \
    X(ApiError_EmailAlreadyBound, -312, "该邮箱已绑定过账号 ") \
    /* --- MFA & Verification Code Errors (-4xx) --- */ \
    X(ApiError_InvalidVerifyCode, -401, "验证码错误 ") \
    X(ApiError_SendVerifyCodeTooFrequent, -402, "发送过于频繁 ") \
    X(ApiError_TargetNotBoundToUser, -403, "目标未绑定到当前用户 ") \
    X(ApiError_UnknownChannelType, -404, "无法判断渠道类型 ") \
    X(ApiError_SendVerifyCodeFailed, -405, "发送验证码失败 ") \
    /* --- Third Party Login Errors (-5xx) --- */ \
    X(ApiError_UnsupportedPlatform, -501, "不支持的第三方平台 ") \
    X(ApiError_PlatformAlreadyBound, -502, "该平台已经绑定过账号 ") \
    X(ApiError_PlatformNotBound, -503, "该平台未绑定过该账号 ") \
    X(ApiError_ThirdPartyAuthFailed, -504, "第三方登陆验证失败 ") \
    X(ApiError_LoginValueNotFound, -505, "找不到对应的登录值 ") \
    X(ApiError_ThirdPartyCallbackFailed, -506, "处理第三方登录回调失败 ") \
    X(ApiError_CodeNotLoggedIn, -507, "该code尚未登录 ") \
    X(ApiError_LoginProcessing, -508, "登录请求处理中 ") \
    X(ApiError_BindingFailed, -509, "绑定第三方账号失败 ") \
    X(ApiError_ThirdPartyInfoCreationFailure, -510, "创建第三方登录信息失败 ") \
    /* --- GitLab Integration Errors (-6xx) --- */ \
    X(ApiError_GitLabAccountCreationFailure, -601, "创建 GitLab 账号失败 ") \
    X(ApiError_GitLabAccountDeletionFailure, -602, "删除 GitLab 用户失败 ") \
    X(ApiError_GitLabProjectInvitationFailure, -603, "邀请用户加入项目失败 ") \
    X(ApiError_GitLabTokenCreationFailure, -604, "创建 GitLab Impersonation Token失败 ")

namespace UEAdminAPI {

    // 1. 定义枚举
    enum ApiErrorCode {
#define X(name, code, msg) name = code,
        API_ERROR_CODE_MAP(X)
#undef X
        ApiError_Unknown = -9999
    };

    // 2. 获取错误信息的辅助函数 (inline 避免链接错误)
    inline const char* GetApiErrorMessage(int code) {
        switch (code) {
#define X(name, code, msg) case name: return msg;
            API_ERROR_CODE_MAP(X)
#undef X
            default: return "未知错误";
        }
    }

} // namespace UEAdminAPI

#endif // API_ERROR_CODES_H
