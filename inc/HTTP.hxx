#pragma once
#include <stdint.hxx>
#include <iostream>
#include <string>
#include <map>
#include <vector>

// Define HttpRequest struct with binary body
struct HttpRequest {
    std::string method;
    std::string url;
    std::map<std::string, std::string> headers;
    std::vector<uint8_t> body;  // Binary body data
	u64 RetryCounter = 0;
};

// HttpResponse struct definition (same as provided in the question)
struct HttpResponse {
    int status_code;
    std::string reason_phrase;
    std::map<std::string, std::string> headers;
    std::vector<u8> body;

	void operator<<(std::string data) {
		for(char c : data) {
			body.push_back(c);
		}
	}

    std::vector<u8> to_binary() const {
        std::ostringstream response;
        response << "HTTP/1.1 " << status_code << " " << reason_phrase << "\r\n";
        for (const auto& [header, value] : headers) {
            response << header << ": " << value << "\r\n";
        }
        response << "\r\n";
		std::vector<u8> buffer;
		for(char c : response.str()) {
			buffer.push_back(c);
		}
		for(u8 b : body) {
			buffer.push_back(b);
		}
        return buffer;
    }
};
