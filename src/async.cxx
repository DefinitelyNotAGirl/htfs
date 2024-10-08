#include <async.hxx>
#include <stdint.hxx>
#include <unistd.h>  // for fork
#include <stdexcept>  // for std::runtime_error
pid_t async(async_function func, u64 microsecondDelay,void* data) {
    pid_t pid = fork();
    if (pid == -1) {
        throw std::runtime_error("Failed to fork process.");
    } else if (pid == 0) {
        func(data);
		if(microsecondDelay)
			usleep(microsecondDelay);
        _exit(0);
    }
	return pid;
}
