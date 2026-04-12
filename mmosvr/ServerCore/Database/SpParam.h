#pragma once

// OUTPUT parameter wrapper
template<typename T>
struct OutParam
{
	T* ptr;
	explicit OutParam(T& ref) : ptr(&ref) {}
};

// INPUT/OUTPUT parameter wrapper
template<typename T>
struct InOutParam
{
	T* ptr;
	explicit InOutParam(T& ref) : ptr(&ref) {}
};

// Factory functions
template<typename T>
OutParam<std::decay_t<T>> Out(T& v) { return OutParam<std::decay_t<T>>{v}; }

template<typename T>
InOutParam<std::decay_t<T>> InOut(T& v) { return InOutParam<std::decay_t<T>>{v}; }

// Compile-time parameter direction detection
template<typename T>
struct IsOutParamType : std::false_type {};

template<typename T>
struct IsOutParamType<OutParam<T> > : std::true_type {};

template<typename T>
struct IsInOutParamType : std::false_type {};

template<typename T>
struct IsInOutParamType<InOutParam<T> > : std::true_type {};

template<typename T>
struct IsOutputParamType : std::bool_constant<
	IsOutParamType<T>::value || IsInOutParamType<T>::value> {};

template<typename... Args>
concept AllInputParams = (!IsOutputParamType<std::decay_t<Args>>::value && ...);

