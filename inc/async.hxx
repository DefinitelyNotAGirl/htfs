#pragma once
#include <stdint.hxx>
typedef void(*async_function)();
void async(async_function func, u64 microsecondDelay = 0);
