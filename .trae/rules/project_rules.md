# 项目规则

1. 本项目基于 Drogon 框架构建，数据库选用 PostgreSQL；所有数据库操作均通过 ORM 完成，不直接编写 SQL 语句，项目整体结构遵循 Drogon 官方示例规范。  
2. 修改代码后请勿触发 CMake 编译流程，因本人使用其他编译器，不会在 VSCode 内执行编译。
3. 各步骤之间请保留空行，避免内容过于紧凑，以提升可读性。
4. 在 .cc/.cpp 文件中可大方使用 `using namespace`，避免冗长的 `drogon::orm::User::Cols::` 等前缀，以提升可读性；相反，在 .h 文件中应尽量避免使用 `using namespace`。
5. 所有服务单例（如 `auto _authService = AuthService::Instance();`）请在函数开头统一获取，置于最前。
6. 在controllers下的接口中, 不包含具体代码, 而是在对应的服务类中实现, 并在接口中调用服务类的方法, controller只做参数读取, 比如POST参数(统一使用PostParamMap类读取), 还有需要授权的接口要从header中读取token.
