#pragma once
#include <string>
#include <map>
#include <sstream>
#include <ostream>
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <sstream>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <HTTP.hxx>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <async.hxx>
#include <sys/wait.h>
class HttpRequestClient {
	virtual u16 DefaultPort(){return 80;}
	void parse_url(const std::string& url, std::string& host, int& port, std::string& path) {
	    size_t host_start = url.find("https://") == 0 ? 8 : 0;
	    size_t host_end = url.find('/', host_start);
	    if (host_end == std::string::npos) {
	        host = url.substr(host_start);
	        path = "/";
	    } else {
	        host = url.substr(host_start, host_end - host_start);
	        path = url.substr(host_end);
	    }
	    port = DefaultPort(); // HTTPS port by default
	}
public:
	virtual std::string GetUserAgent() {
		return "HTFS/1.0";
	}
	virtual void OnSend(HttpRequest& request) {
		request.headers.insert({"User-Agent", GetUserAgent()});
	}
	virtual void OnResponse(HttpRequest& request,HttpResponse& response) {
	}
	virtual HttpResponse Send(HttpRequest& request) {
		OnSend(request);
	    // Extract host, port, and path from URL
	    std::string host, path;
	    int port;
	    parse_url(request.url, host, port, path);
	    // Create a socket and connect to the server
	    int sockfd;
	    struct sockaddr_in server_addr;
	    struct hostent* server = gethostbyname(host.c_str());
	    if (!server) {
			throw std::runtime_error("No such host: "+host);
	    }
	    sockfd = socket(AF_INET, SOCK_STREAM, 0);
	    if (sockfd < 0) {
	        throw std::runtime_error("Error creating socket: "+std::string(strerror(errno)));
	    }
	    memset((void*)&server_addr,0, sizeof(server_addr));
	    server_addr.sin_family = AF_INET;
		memcpy((void*)&server_addr.sin_addr.s_addr,(void*)server->h_addr,server->h_length);
	    server_addr.sin_port = htons(port);
	    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
	        throw std::runtime_error("Error connecting to "+host+":"+std::to_string(port)+" "+std::string(strerror(errno)));
	    }
	    // Build HTTP request with binary data in the body
	    std::ostringstream http_request;
	    http_request << request.method << " " << path << " HTTP/1.1\r\n";
	    http_request << "Host: " << host << "\r\n";
	    for (const auto& [header, value] : request.headers) {
	        http_request << header << ": " << value << "\r\n";
	    }
	    if (!request.body.empty()) {
	        http_request << "Content-Length: " << request.body.size() << "\r\n";
	    }
	    http_request << "\r\n";  // End of headers
	    // Send headers and body (in chunks)
	    std::string request_headers = http_request.str();
		//std::cout << "debug 0" << std::endl;
	    write(sockfd, request_headers.c_str(), request_headers.length());
	    if (!request.body.empty()) {
	        write(sockfd, reinterpret_cast<const char*>(request.body.data()), request.body.size());
	    }
	    // Receive response
	    char buffer[8192];
	    int bytes_read = 0;
	    std::ostringstream response_stream;
		//std::cout << "sockfd: " << sockfd << std::endl;
		//std::cout << "buffer: " << buffer << std::endl;
		//std::cout << "sizeof(buffer): " << sizeof(buffer) << std::endl;
		//std::cout << "starting http read" << std::endl;
		struct timeoutdata {
			char** buffer;
			int* bytes_read;
			std::ostringstream* response_stream;
			int* sockfd;
			int bufferSize;
		};
		timeoutdata td; {
			td.buffer = (char**)&buffer;
			td.bytes_read = &bytes_read;
			td.response_stream = &response_stream;
			td.sockfd = &sockfd;
			td.bufferSize = sizeof(buffer);
		}
		pid_t rtid = async([](void* td){
			while ((*(((timeoutdata*)td)->bytes_read) = read(*(((timeoutdata*)td)->sockfd), *(((timeoutdata*)td)->buffer), (((timeoutdata*)td)->bufferSize))) > 0) {
				//std::cout << "bytes read: " << bytes_read << std::endl;
	    	    (((timeoutdata*)td)->response_stream)->write(*(((timeoutdata*)td)->buffer), *(((timeoutdata*)td)->bytes_read));
	    	}
		},0,&td);
		async([](void* tid){
			kill((pid_t)(intptr_t)tid,SIGTERM);
		},2000,(void*)rtid);
		int status;
		waitpid(rtid,&status,0);
		//std::cout << "finished http read" << std::endl;
		//std::cout << "bytes read: " << bytes_read << std::endl;
	    close(sockfd);
		//std::cout << "debug 1" << std::endl;
	    // Parse the response
	    std::string response_str = response_stream.str();
	    HttpResponse response;
	    // Parse status line (e.g., "HTTP/1.1 200 OK")
	    std::istringstream response_lines(response_str);
	    std::string http_version;
	    response_lines >> http_version >> response.status_code >> std::ws;
	    std::getline(response_lines, response.reason_phrase);
	    // Parse headers
	    std::string header_line;
	    while (std::getline(response_lines, header_line) && header_line != "\r") {
	        size_t delimiter = header_line.find(':');
	        if (delimiter != std::string::npos) {
	            std::string header_name = header_line.substr(0, delimiter);
	            std::string header_value = header_line.substr(delimiter + 2, header_line.size() - delimiter - 3);  // Strip '\r'
	            response.headers[header_name] = header_value;
	        }
	    }
	    // Parse body
	    std::ostringstream body_stream;
	    body_stream << response_lines.rdbuf();
	    response << body_stream.str();
		OnResponse(request,response);
	    return response;
	}
};
