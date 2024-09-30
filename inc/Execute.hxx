#pragma once
#include <string>
#include <fcntl.h>     // For shm_open, O_CREAT, O_RDWR
#include <sys/mman.h>  // For mmap, PROT_READ, PROT_WRITE
#include <unistd.h>    // For ftruncate, execv
#include <cstring>     // For strcpy
#include <iostream>
#include "stdint.hxx"
#include <dlfcn.h>

inline u32 GetUnixTimestamp() {
	const auto p1 = std::chrono::system_clock::now();
	return std::chrono::duration_cast<std::chrono::seconds>(p1.time_since_epoch()).count();
}

/*
inline void release_shared_memory(const char *shm_name, void *shm_ptr, size_t shm_size) {
    // Unmap the shared memory from the process's address space
    if (munmap(shm_ptr, shm_size) == -1) {
        perror("munmap");
    } else {
        //std::cout << "Shared memory unmapped successfully." << std::endl;
    }
    // Unlink (delete) the shared memory object
    if (shm_unlink(shm_name) == -1) {
        perror("shm_unlink");
    } else {
       // std::cout << "Shared memory unlinked (released) successfully." << std::endl;
    }
}

template<typename InputDataType,typename OutputDataType>
OutputDataType Execute(std::string ExecPath,InputDataType& Input) {
	std::string InputName = "/cii"+std::to_string(GetUnixTimestamp());
	std::string OutputName = "/cio"+std::to_string(GetUnixTimestamp());
	OutputDataType* SharedOutput;
	InputDataType* SharedInput;
	int out_fd;
	int in_fd;
	//+
	//+ create shared memory for input
	//+
	{
		// Create the shared memory object
    	int shm_fd = shm_open(InputName.c_str(), O_CREAT | O_RDWR, 0666);
		in_fd = shm_fd;
    	if (shm_fd == -1) {
    	    throw std::string("input shm_open failed: "+std::string(strerror(errno)));
    	}
    	// Set the size of the shared memory object
    	if (ftruncate(shm_fd, sizeof(InputDataType)) == -1) {
    	    throw std::string("input ftruncate failed: "+std::string(strerror(errno)));
    	}
    	// Map the shared memory object into the process’s address space
    	void* shm_ptr = mmap(0, sizeof(InputDataType), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    	if (shm_ptr == MAP_FAILED) {
			throw std::string("input map failed: "+std::string(strerror(errno)));
    	}
		SharedInput = (InputDataType*)shm_ptr;
    	memcpy(shm_ptr,&Input,sizeof(InputDataType));
    	// Close the shared memory object (the memory is still mapped)
    	close(shm_fd);
	}
	//+
	//+ create shared memory for output
	//+
	{
		// Create the shared memory object
    	int shm_fd = shm_open(OutputName.c_str(), O_CREAT | O_RDWR, 0666);
		out_fd = shm_fd;
    	if (shm_fd == -1) {
    	    throw std::string("output shm_open failed: "+std::string(strerror(errno)));
    	}
    	// Set the size of the shared memory object
    	if (ftruncate(shm_fd, sizeof(OutputDataType)) == -1) {
			release_shared_memory(InputName.c_str(),SharedInput,in_fd);
    	    throw std::string("output ftruncate failed: "+std::string(strerror(errno)));
    	}
    	// Map the shared memory object into the process’s address space
    	void* shm_ptr = mmap(0, sizeof(OutputDataType), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    	if (shm_ptr == MAP_FAILED) {
			release_shared_memory(InputName.c_str(),SharedInput,in_fd);
			throw std::string("output map failed: "+std::string(strerror(errno)));
    	}
		SharedOutput = (OutputDataType*)shm_ptr;
		memset(shm_ptr,0,sizeof(OutputDataType));
    	// Close the shared memory object (the memory is still mapped)
    	close(shm_fd);
	}
	//.
	//. run executable
	//.
	{
		int pid = fork();
		if(pid == -1) {
			release_shared_memory(InputName.c_str(),SharedInput,in_fd);
			release_shared_memory(OutputName.c_str(),SharedOutput,out_fd);
			throw std::string("fork failed: "+std::string(strerror(errno)));
		}
		if(pid == 0) {
			// Arguments to pass to the child process
    		char *args[] = {ExecPath.data(),InputName.data(),OutputName.data(), nullptr};
    		// Execute the child program
    		execv(ExecPath.c_str(), args);
			exit(-1);
		}
		int status;
        pid_t wait_result = waitpid(pid, &status, 0);
		if (wait_result == -1) {
			release_shared_memory(InputName.c_str(),SharedInput,in_fd);
			release_shared_memory(OutputName.c_str(),SharedOutput,out_fd);
			throw std::string("Error waiting for child process: " + std::string(strerror(errno)));
        }
		OutputDataType Output = *SharedOutput;
		release_shared_memory(InputName.c_str(),SharedInput,sizeof(InputDataType));
		release_shared_memory(OutputName.c_str(),SharedOutput,sizeof(OutputDataType));
		return Output;
	}
}
*/

template<typename InputDataType,typename OutputDataType>
OutputDataType Execute(std::string ExecPath,InputDataType& Input) {
	typedef OutputDataType(*FunctionType)(InputDataType&);
	system(std::string(std::string("make -C ")+ExecPath.substr(0,ExecPath.find_last_of('/'))).c_str());
	void* handle = dlopen(ExecPath.c_str(), RTLD_LAZY);
    if (!handle) {
        throw std::string("Failed to load library: " + std::string(dlerror()));
    }
    // Clear any existing errors
    dlerror();
    // Load symbol
	FunctionType func = (FunctionType)dlsym(handle, "run");
    const char* dlsym_error = dlerror();
    if (dlsym_error) {
		throw std::string("Failed to load symbol: " + std::string(dlsym_error));
        dlclose(handle);
    }
    // Call the dynamically loaded function
    OutputDataType output = func(Input);
    // Close the library
    dlclose(handle);
    return output;
}
