#include <async.hxx>
#include <stdint.hxx>
#include <unistd.h>  // for fork
#include <sys/types.h>  // for pid_t
#include <stdexcept>  // for std::runtime_error
void async(async_function func, u64 microsecondDelay) {
    pid_t pid = fork();
    if (pid == -1) {
        throw std::runtime_error("Failed to fork process.");
    } else if (pid == 0) {
        func();
		if(microsecondDelay)
			usleep(microsecondDelay);
        _exit(0);
    }
}
