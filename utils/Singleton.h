#pragma once

#include<memory>

template<class T>
class Singleton {
public:
	static T* GetInstance() {
		static T v;
		return &v;
	}
};

template<class T>
class SingletonPtr {
public:
	static std::shared_ptr<T> GetInstance() {
		static std::shared_ptr<T> v(new T);
		return v;
	}
};
