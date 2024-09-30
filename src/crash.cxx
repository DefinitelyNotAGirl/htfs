       #include <csignal>
#include <stacktrace.hxx>

extern int server_fd;
static void MemoryAccessViolation(int sig)
{
	if(server_fd != 0) {
		close(server_fd);
	}
	std::cout << "\u001b[41m\n                              memory access violation\n";
	std::cout << "signal: " << sig << "\n";
    printStacktrace(50);
	std::cout << "\u001b[0m" << std::endl;
	exit(1);
}

static void AbnormalTermination(int sig) 
{
	if(server_fd != 0) {
		close(server_fd);
	}
	std::cout << "\u001b[41m\n                              abnormal termination\n";
	std::cout << "signal: " << sig << "\n";
    printStacktrace(50);
	std::cout << "\u001b[0m" << std::endl;
	exit(1);
}

static void InvalidInstruction(int sig) 
{
	if(server_fd != 0) {
		close(server_fd);
	}
	std::cout << "\u001b[41m\n                              invalid instruction\n";
	std::cout << "signal: " << sig << "\n";
    printStacktrace(50);
	std::cout << "\u001b[0m" << std::endl;
	exit(1);
}

static void KeyboardInterrupt(int sig) 
{
	if(server_fd != 0) {
		close(server_fd);
	}
	exit(0);
}

__attribute__((constructor)) void install_crash_handlers()
{
	signal(SIGSEGV, MemoryAccessViolation);
    signal(SIGABRT, AbnormalTermination);
    signal(SIGILL, InvalidInstruction);
	signal(SIGINT, KeyboardInterrupt);
}
