#pragma once
#include <stdint.hxx>
#include <sys/types.h>  // for pid_t
typedef void(*async_function)(void* data);
pid_t async(async_function func, u64 microsecondDelay = 0,void* data = nullptr);
