#define HTTP_ACCESS public

#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <iostream>
#include <map>
#include <string>
#include <sstream>
#include <vector>
#include <Execute.hxx>
#include <Generic.hxx>
#include <JSON.hxx>
#include <HTTP.hxx>
#include <async.hxx>
#include <util.hxx>
#include <filesystem>
#include <fstream>
#include <fmt/core.h>
#include <sys/xattr.h>
#include <stdexcept>
#include <stacktrace.hxx>

//+
//+
//+ server config
//+
//+
#ifdef platform_apple
	const std::string root = "/Volumes/programming/htfs/srv";
#else
	const std::string root = "/htfs/srv";
#endif

#define BUFFER_SIZE 4096

auto read_file(std::string_view path) -> std::string {
    constexpr auto read_size = std::size_t(4096);
    auto stream = std::ifstream(path.data());
    stream.exceptions(std::ios_base::badbit);

    if (not stream) {
        throw std::ios_base::failure("file does not exist");
    }
    
    auto out = std::string();
    auto buf = std::string(read_size, '\0');
    while (stream.read(& buf[0], read_size)) {
        out.append(buf, 0, stream.gcount());
    }
    out.append(buf, 0, stream.gcount());
    return out;
}

// Returns:
//   true upon success.
//   false upon failure, and set the std::error_code & err accordingly.
bool CreateDirectoryRecursive(std::string const & dirName, std::error_code & err)
{
    err.clear();
    if (!std::filesystem::create_directories(dirName, err))
    {
        if (std::filesystem::exists(dirName))
        {
            // The folder already exists:
            err.clear();
            return true;    
        }
        return false;
    }
    return true;
}

static u64 StringToInt(const std::string& str,u64 base) {
	return strtoull(str.c_str(),(char**)(str.c_str()+str.length()),base);
}

class ClientSpecificFileInfo {
public:
	std::string OriginalPath;

	void save(std::string file) {
		//std::cout << "saving info to file: " << file << std::endl;
		FILE* f = fopen(file.c_str(),"w");
		fwrite((void*)this->OriginalPath.c_str(),this->OriginalPath.size(),1,f);
		fclose(f);
	}

	ClientSpecificFileInfo() {
	}

	ClientSpecificFileInfo(std::string file) {
		this->OriginalPath = read_file(file);
	}
};

static std::string DoubleToDatasizeString(double d) {
	return fmt::format("{:.2f}", d);
}

static std::string IntToDataSize(i64 n) {
	std::string res;
	if(n < 0) {
		n*=-1;
		res.push_back('-');
	}
	if(n >= 1024*1024*1024) {
		res += DoubleToDatasizeString(((double)n)/((double)1024*1024*1024))+"GiB";
	} else if(n >= 1024*1024) {
		res += DoubleToDatasizeString(((double)n)/((double)1024*1024))+"MiB";
	} else if(n >= 1024) {
		res += DoubleToDatasizeString(((double)n)/((double)1024))+"KiB";
	} else {
		res += std::to_string(n) + "B";
	}
	return res;
}

static std::string ByteVectorToString(std::vector<u8> data) {
	std::string value;
	value.resize(data.size());
	memcpy((void*)value.data(),(void*)data.data(),data.size());
	return value;
}
static std::vector<u8> StringToByteVector(std::string data) {
	std::vector<u8> value;
	value.resize(data.size());
	memcpy((void*)value.data(),(void*)data.data(),data.size());
	return value;
}

#ifdef platform_apple
std::vector<u8> GetExtendedAttribute(const std::string& Path, const std::string& AttributeName) {
    // First, find the size of the extended attribute
    ssize_t size = getxattr(Path.c_str(), AttributeName.c_str(), nullptr, 0, 0, 0);
    if (size == -1) {
        throw std::runtime_error("Error getting size of extended attribute");
    }
    // Allocate a buffer for the attribute value
    std::vector<u8> value(size);
    // Retrieve the attribute
    ssize_t result = getxattr(Path.c_str(), AttributeName.c_str(), value.data(), value.size(), 0, 0);
    if (result == -1) {
        throw std::runtime_error("Error getting extended attribute");
    }
    return value;
}
void SetExtendedAttribute(const std::string& path, const std::string& attribute_name, const std::vector<u8>& value) {
    // Set the extended attribute
    ssize_t result = setxattr(path.c_str(), attribute_name.c_str(), value.data(), value.size(), 0, 0);
    if (result == -1) {
        throw std::runtime_error("Error setting extended attribute");
    }
}
#endif
#ifdef platform_linux
void SetExtendedAttribute(const std::string& path, const std::string& attribute_name, const std::vector<u8>& value) {
    // Set the extended attribute (Linux variant)
    ssize_t result = setxattr(path.c_str(), attribute_name.c_str(), value.data(), value.size(), 0);
    if (result == -1) {
        throw std::runtime_error("Error setting extended attribute");
    }
}
std::vector<u8> GetExtendedAttribute(const std::string& path, const std::string& attribute_name) {
    // First, get the size of the extended attribute
    ssize_t size = getxattr(path.c_str(), attribute_name.c_str(), nullptr, 0);
    if (size == -1) {
        throw std::runtime_error("Error getting size of extended attribute");
    }
    // Allocate buffer and retrieve the attribute
    std::vector<u8> value(size);
    ssize_t result = getxattr(path.c_str(), attribute_name.c_str(), value.data(), value.size());
    if (result == -1) {
        throw std::runtime_error("Error getting extended attribute");
    }
    return value;
}
#define ENOATTR ENODATA
#endif
static std::string GetOriginalClientPath(const std::string& file,const std::string& client_ip) {
	try {
		std::string attrname = "user.htfs.ocp."+client_ip;
		std::string path = ByteVectorToString(GetExtendedAttribute(file,attrname));
		return path;
	} catch(std::runtime_error) {
		if (errno == ENOATTR || errno == ENODATA) {
    	    // ENOATTR (macOS) or ENODATA (Linux) indicates the attribute doesn't exist
    	    return "";
    	} else {
    	    throw;
    	}
	}
}
static void SetOriginalClientPath(const std::string& file,const std::string& client_ip,const std::string& ClientPath) {
	std::string attrname = "user.htfs.ocp."+client_ip;
	SetExtendedAttribute(file,attrname,StringToByteVector(ClientPath));
}

static bool ProcessRequest(HttpResponse& response,const std::string& client_ip, const std::string& method, const std::string& url, const std::map<std::string, std::string>& headers, const std::map<std::string, std::string> &query_params, const void* body,int body_size) {
	if(method == "POST") {
		std::string ServerPath = root + url;
		if(std::filesystem::is_directory(ServerPath)) {
			response.status_code = 409;
			response.reason_phrase = "specified path is a directory on the server";
			return true;
		}
		std::string ClientPath = "";
		response.status_code = 201;
		response.reason_phrase = std::filesystem::exists(ServerPath) ? "UPDATED" : "CREATED";
		try {
			ClientPath = headers.at("original-path");
		}catch(std::out_of_range){}
		{
			std::error_code err;
			std::string dir = ServerPath.substr(0,ServerPath.find_last_of("/"));
			//std::cout << "dir: " << dir << std::endl;
			CreateDirectoryRecursive(dir,err);
		}
		i64 PreviousSize = 0;
		if(std::filesystem::exists(ServerPath)) {
			PreviousSize = std::filesystem::file_size(ServerPath);
		}
		i64 SizeDelta = body_size - PreviousSize;
		std::cout << "\x1b[32mPOST " << url << "   |   " << (SizeDelta < 0 ? "" : "+") << IntToDataSize(SizeDelta) /*<< "(uploaded: "<< IntToDataSize(body_size) <<" - on file: "<< IntToDataSize(PreviousSize) <<")"*/ << "\x1b[0m" << std::endl;
		FILE* f = fopen(ServerPath.c_str(),"w");
		fwrite((void*)body,body_size,1,f);
		fclose(f);
		SetOriginalClientPath(ServerPath,client_ip,ClientPath);
		return true;
	} else if(method == "GET") {
		std::string ServerPath = root + url;
		if(std::filesystem::is_directory(ServerPath)) {
			response.status_code = 200;
			response.reason_phrase = "OK";
			std::filesystem::path pp = std::filesystem::path(ServerPath);
			for(std::filesystem::path p : pp) {
				response << p.generic_string();
				response.body.push_back('\n');
			}
			if(response.body.back() == '\n')
				response.body.pop_back();
			return true;
		} else if(std::filesystem::exists(ServerPath)) {
			response.status_code = 200;
			response.reason_phrase = "OK";
			response.headers.insert({"original-path",GetOriginalClientPath(ServerPath,client_ip)+"\n"});
			{
				// Open the file in binary mode
    			std::ifstream file(ServerPath, std::ios::binary | std::ios::ate);
    			if (!file.is_open()) {
    			    throw std::runtime_error("Failed to open file");
    			}
    			// Get the file size
    			std::streamsize size = file.tellg();
    			file.seekg(0, std::ios::beg);
    			// Read the file into the buffer
				u64 offset = response.body.size();
				response.body.resize(response.body.size()+size);
    			if (!(file.read((char*)response.body.data()+offset, size))) {
    			    throw std::runtime_error("Error reading file");
    			}
				std::cout << "\x1b[33mGET " << url << "   |   " << IntToDataSize(size) << "\x1b[0m" << std::endl;
			}
			return true;
		}
	} else {
		response.status_code = 405;
    	response.reason_phrase = "method not allowed";
    	response.headers = {
    	    {"Content-Type", "text/plain"},
    	    {"Connection", "close"}
    	};
    	response << method+" invalid for endpoint "+url;
		return true;
	}
	return false;
}

// Function to handle the HTTP request
static HttpResponse OnHttpRequest(const std::string& client_ip, const std::string& method, const std::string& url, const std::map<std::string, std::string>& headers, const std::map<std::string, std::string> &query_params, const void* body,int body_size) {
    try {
    	//std::cout << "\x1B[31m" << client_ip << "\x1B[32m " << method << "\x1B[33m " << url << "\x1B[0m" << std::endl;
		HttpResponse response;
		if(ProcessRequest(response,client_ip,method,url,headers,query_params,body,body_size))
    		return response;
		response.status_code = 404;
    	response.reason_phrase = "endpoint not found";
		response.headers.clear();
    	response.headers = {
    	    {"Content-Type", "text/plain"},
    	    {"Connection", "close"}
    	};
    	response << "endpoint "+url+" not found";
    	return response;
	} catch(std::exception e) {
		HttpResponse response;
    	response.status_code = 500;
    	response.reason_phrase = "internal server error";
    	response.headers = {
    	    {"Content-Type", "text/plain"},
    	    {"Connection", "close"}
    	};
    	response << e.what();
    	return response;
	} catch(JSONNoSuchChild e) {
		HttpResponse response;
    	response.status_code = 400;
    	response.reason_phrase = "bad request";
    	response.headers = {
    	    {"Content-Type", "text/plain"},
    	    {"Connection", "close"}
    	};
     	response << "missing property: "+e.ChildName;
    	return response;
	} catch(std::string s) {
		HttpResponse response;
    	response.status_code = 500;
    	response.reason_phrase = "internal server error";
    	response.headers = {
    	    {"Content-Type", "text/plain"},
    	    {"Connection", "close"}
    	};
    	response << "error: "+s;
    	return response;
	} catch(...) {
		HttpResponse response;
    	response.status_code = 500;
    	response.reason_phrase = "internal server error";
    	response.headers = {
    	    {"Content-Type", "text/plain"},
    	    {"Connection", "close"}
    	};
    	response << "unknown error";
		throw;
    	return response;
	}
}

#include <iostream>
#include <string>
#include <map>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

#define BUFFER_SIZE 4096

// Function to parse query parameters from the URL
std::map<std::string, std::string> parse_query_params(const std::string &url) {
    std::map<std::string, std::string> query_params;
    size_t query_start = url.find("?");
    if (query_start == std::string::npos) return query_params; // No query parameters

    std::string query_string = url.substr(query_start + 1);
    std::stringstream ss(query_string);
    std::string pair;

    while (std::getline(ss, pair, '&')) {
        size_t equals_pos = pair.find('=');
        if (equals_pos != std::string::npos) {
            std::string key = pair.substr(0, equals_pos);
            std::string value = pair.substr(equals_pos + 1);
            query_params[key] = value;
        }
    }
    return query_params;
}

// Simple function to parse HTTP headers from a request string
std::map<std::string, std::string> parse_headers(const std::string &raw_headers) {
    std::map<std::string, std::string> headers;
    std::stringstream request_stream(raw_headers);
    std::string line;

    while (std::getline(request_stream, line) && line != "\r") {
        size_t delimiter_pos = line.find(": ");
        if (delimiter_pos != std::string::npos) {
            std::string key = line.substr(0, delimiter_pos);
            std::string value = line.substr(delimiter_pos + 2);
            headers[key] = value;
        }
    }

    return headers;
}

// Function to read HTTP request (headers + body) from the client socket
std::string read_http_request(int client_socket) {
    char buffer[BUFFER_SIZE];
    std::string request;
    ssize_t bytes_read;

    // Read from the socket until we have the full header (detecting the end of headers \r\n\r\n)
    while ((bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1)) > 0) {
        buffer[bytes_read] = '\0';
        request += buffer;

        // Check if we have reached the end of the headers
        if (request.find("\r\n\r\n") != std::string::npos) {
            break;
        }
    }

    return request;
}

// Function to read the body based on the Content-Length header
void* read_body(int socket, u64 content_length, u64& bytes_read) {
	//std::cout << "reading body, content_length: " << content_length << std::endl;
    char* body = new char[content_length + 1]; // +1 for null terminator
    int read_result = 0;

	//std::cout << "bytes_read " << bytes_read << std::endl;
	//std::cout << "content_length " << content_length << std::endl;
    while (bytes_read < content_length) {
		//std::cout << "bytes_read " << bytes_read << std::endl;
        // Try to read the remaining data
        read_result = read(socket, body + bytes_read, content_length - bytes_read);
        
        // Handle possible errors
        if (read_result < 0) {
            perror("Error reading body");
            delete[] body;
            return nullptr;
        } else if (read_result == 0) {
			if(body) {
				delete[] body;
			}
            throw std::runtime_error("Client closed the connection prematurely");
        }

        // Update the number of bytes read
        bytes_read += read_result;
    }

    body[content_length] = '\0'; // Null-terminate the body
    return body;
}

#include <netinet/tcp.h> // for TCP_NODELAY

void set_socket_nodelay(int socket) {
    int flag = 1;
    if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int)) < 0) {
        perror("TCP_NODELAY failed");
    }
}

// Basic HTTPS server function
int server_fd;
void start_http_server(int port) {
    int new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Bind to any network interface
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    std::cout << "HTTP Server listening on port " << port << std::endl;

    while (true) {
		try {
			if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
        	    perror("Accept failed");
        	    exit(EXIT_FAILURE);
        	}
			set_socket_nodelay(new_socket);
        	// Get the client's address dynamically
        	struct sockaddr_in client_address;
        	socklen_t client_len = sizeof(client_address);
        	if (getpeername(new_socket, (struct sockaddr *)&client_address, &client_len) == -1) {
        	    perror("Failed to get client address");
        	    exit(EXIT_FAILURE);
        	}
        	char client_ip[INET_ADDRSTRLEN];
        	inet_ntop(AF_INET, &(client_address.sin_addr), client_ip, INET_ADDRSTRLEN);
        	// Read the HTTP request from the SSL connection
        	std::string raw_request = read_http_request(new_socket);
        	// Parse the request (split headers and body)
        	std::string header_part = raw_request.substr(0, raw_request.find("\r\n\r\n"));
        	std::map<std::string, std::string> headers = parse_headers(header_part);
        	// Parse the HTTP method and URL
        	std::istringstream request_stream(raw_request);
        	std::string method, url, version;
        	request_stream >> method >> url >> version;
        	// Parse query parameters
        	std::map<std::string, std::string> query_params = parse_query_params(url);
        	//Check for Content-Length to determine if a body exists
    		u64 content_length = headers.count("Content-Length") ? std::stoull(headers["Content-Length"]) : 0;
			u64 bOffset = raw_request.find("\r\n\r\n")+4;
			u64 bytes_read = (raw_request.length())-(bOffset);
    		void* body;
    		if ((content_length-bytes_read) > 0) {
    		    body = read_body(new_socket, content_length, bytes_read);
    		} else {
				body = new u8[content_length+1];
			}
			//std::cout << "body: " << body << std::endl;
			//std::cout << "src: " << (void*)(raw_request.c_str()+bOffset) << std::endl;
			//std::cout << "src: " << (size_t)(raw_request.length())-(bOffset) << std::endl;
			memcpy(body,(void*)(raw_request.c_str()+bOffset),(raw_request.length())-(bOffset));
			//u64 content_length = raw_request.size()-bOffset;
        	// Call the request handler
        	HttpResponse response = OnHttpRequest(client_ip, method, url, headers, query_params, body, content_length);
        	// Send the response to the client
        	std::vector<u8> http_response = response.to_binary();
        	send(new_socket, http_response.data(), http_response.size(), 0);
			//if (body) delete[] static_cast<char*>(body);
        	close(new_socket);
		} catch(std::runtime_error e) {
			std::cerr << "\x1b[31mruntime error: " << e.what() <<"\x1b[0m"<< std::endl;
			close(new_socket);
		}
    }
    close(server_fd);
}

#include <HttpRequestClient.hxx>
class HtfsRequestClient : public HttpRequestClient {
	virtual u16 DefaultPort(){return 20;}
protected:
public:
	HtfsRequestClient(){}

	virtual void OnSend(HttpRequest& request) {
		HttpRequestClient::OnSend(request);
	}

	virtual void OnResponse(HttpRequest& request,HttpResponse& response) {
	}
};

void UploadFile(HttpRequestClient& client,std::string ClientPath,std::string ServerPath) {
	std::cout << "uploading file: " << ClientPath << std::endl;
	HttpRequest request;
	{
		// Open the file in binary mode
    	std::ifstream file(ClientPath, std::ios::binary | std::ios::ate);
    	if (!file.is_open()) {
    	    throw std::runtime_error("Failed to open file");
    	}
    	// Get the file size
    	std::streamsize size = file.tellg();
    	file.seekg(0, std::ios::beg);
    	// Read the file into the buffer
		u64 offset = request.body.size();
		request.body.resize(request.body.size()+size);
    	if (!(file.read((char*)request.body.data()+offset, size))) {
    	    throw std::runtime_error("Error reading file");
    	}
	}
	request.headers.insert({"original-path",ClientPath});
	request.method = "POST";
	std::cout << "Server path: " << ServerPath << std::endl;
	std::cout << "url: " << request.url << std::endl;
	std::cout << "url.size(): " << request.url.size() << std::endl;
	std::cout << "ServerPath.size(): " << ServerPath.size() << std::endl;
	request.url = std::string("192.168.178.102/")+std::string(ServerPath);
	//std::cout << "Client path: " << ClientPath << std::endl;
	std::cout << "\x1B[34mUPLOAD \x1B[35m" << ClientPath << "\x1B[0m...";
	HttpResponse response = client.Send(request);
	std::cout << "\b\b\b ";
	if(response.status_code >= 200 && response.status_code < 300)std::cout << "\x1B[32m";
	else if(response.status_code >= 300 && response.status_code < 400)std::cout << "\x1B[33m";
	else if(response.status_code >= 400 && response.status_code < 600)std::cout << "\x1B[31m";
	std::cout << response.status_code << " " << response.reason_phrase << "\x1B[0m" << std::endl;;
}

void UploadDirectory(HttpRequestClient& client,std::string ClientPath,std::string ServerPath) {
	std::cout << "uploading directory: " << ClientPath << std::endl;
	std::filesystem::path dir_path = std::filesystem::path(ServerPath);
	for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
		if(std::filesystem::is_directory(entry.path())) {
			UploadDirectory(client,ClientPath+"/"+entry.path().filename().string(),ServerPath+"/"+entry.path().filename().string());
		} else {
			UploadFile(client,ClientPath+"/"+entry.path().filename().string(),ServerPath+"/"+entry.path().filename().string());
		}
    }
}

void DownloadFile(HttpRequestClient& client,std::string& ServerPath) {
	HttpRequest request;
	request.method = "GET";
	request.url = "192.168.178.102/"+ServerPath;
	std::cout << "\x1B[34mDOWNLOAD \x1B[35m" << ServerPath << "\x1B[0m...";
	HttpResponse response = client.Send(request);
	std::cout << "\b\b\b ";
	if(response.status_code >= 200 && response.status_code < 300)std::cout << "\x1B[32m";
	else if(response.status_code >= 300 && response.status_code < 400)std::cout << "\x1B[33m";
	else if(response.status_code >= 400 && response.status_code < 600)std::cout << "\x1B[31m";
	std::cout << response.status_code << " " << response.reason_phrase;
	if(response.status_code == 200) {
		std::cout << "\x1B[0m ==> \x1B[36m" << response.headers.at("original-path");
		FILE* f = fopen(response.headers.at("original-path").c_str(),"w");
		fwrite(response.body.data()+2,response.body.size()-2,1,f);
		fclose(f);
	}
	std::cout << "\x1B[0m" << std::endl;
}



int main(int argc,char** argv) {
	if(argc < 2) {
		std::cerr << "ERROR: you must provide at least one argument: server, up, down or delete" << std::endl;
		return 1;
	}
	try {
		if(std::string(argv[1]) == "server") {
    		start_http_server(20);
		} else {
			HtfsRequestClient Client;
			std::string WorkingDirectory = std::filesystem::current_path().string();
			if(argc < 3) {
				std::cerr << "ERROR: no file path provided" << std::endl;
				return 1;
			}
			std::string ClientPath = argv[2];
			if(std::string(argv[1]) == "up") {
				if(std::filesystem::is_directory(ClientPath)) {
					UploadDirectory(Client,WorkingDirectory+"/"+ClientPath,ClientPath);
				} else {
					UploadFile(Client,WorkingDirectory+"/"+ClientPath,ClientPath);
				}
			}
			else if(std::string(argv[1]) == "down") {
				DownloadFile(Client,ClientPath);
			}
		}
	} catch(std::runtime_error e) {
		std::cerr << "\x1b[31mruntime error: " << e.what() <<"\x1b[0m"<< std::endl;
		return -1;
	}
    return 0;
}
