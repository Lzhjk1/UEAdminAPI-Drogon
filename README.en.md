# UEAdminAPI-drogon

## Project Overview
UEAdminAPI is a management API service developed based on the Drogon framework, primarily designed for GitLab user management, third-party login, and multi-factor authentication (MFA). This project integrates with the GitLab API and provides core functionalities such as registration, login, and inviting users to join projects.

### Key Features
- **GitLab User Management**: Create, delete, modify user passwords, and generate or delete impersonation tokens.
- **User Invitation to Projects**: Administrators can invite users to join GitLab projects.
- **Multi-Factor Authentication (MFA)**: Supports sending verification codes via email and SMS.
- **Third-Party Login**: Supports third-party login from multiple platforms.
- **Registration and Login**: Supports registration and login via email, phone number, and password.

## Installation Guide
This project depends on the Drogon framework and several third-party libraries (e.g., JWT, Tencent SDK). Ensure the following dependencies are installed:
- Drogon framework
- jwt-cpp library
- Tencent SDK (for SMS services)

### Building the Project
1. Ensure CMake and the build toolchain are installed.
2. Run the following commands in the project root directory:
   ```bash
   mkdir build && cd build
   cmake ..
   make
   ```

## Usage Examples
### Example 1: Initialize Singleton Service
```cpp
// Initialize ExampleService
ExampleService::Init("Example", 100);
auto service = ExampleService::Instance();
```

### Example 2: Register a New User
```cpp
// Send registration request
auto req = HttpRequest::newHttpRequest();
req->setMethod(Post);
req->setPath("/api/register");

Json::Value reqJson;
reqJson["username"] = "new_user";
reqJson["password"] = "password123";
reqJson["email"] = "user@example.com";
reqJson["verifyCode"] = "123456";

req->setBody(reqJson.toStyledString());

// Send request and handle response
auto resp = co_await Register::RegisterUser(req);
```

### Example 3: Invite User to Join a Project
```cpp
// Send invitation request
auto req = HttpRequest::newHttpRequest();
req->setMethod(Post);
req->setPath("/api/gitlab/project/invite");

Json::Value reqJson;
reqJson["userId"] = "123";
reqJson["projectId"] = "456";
reqJson["accessLevel"] = "30"; // Access level

req->setBody(reqJson.toStyledString());

// Send request and handle response
auto resp = co_await GitlabController::inviteToProject(req);
```

## Contribution Guidelines
Contributions from the community are welcome. Please follow these steps:
1. Fork the project repository.
2. Create a new branch and make your changes.
3. Submit a Pull Request with a description of your modifications.

## License
This project is licensed under the MIT License. For details, see the [LICENSE](LICENSE/LICENSE-SMTPMail-drogon) file.

## Documentation
- [SingletonWithInit Usage Guide](docs/SingletonWithInitUsage.md)