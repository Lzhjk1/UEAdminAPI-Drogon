#pragma once

#include <iostream>

template<typename T>
class ServiceResponse {
private:
	T Data;
	bool Success;
	std::string Message;

public:
	ServiceResponse(T data, bool success, std::string message) {
        Data = data;
        Success = success;
        Message = message;
	}
    ServiceResponse(){
        Data = T();
        Success = false;
        Message = "";
    }
};