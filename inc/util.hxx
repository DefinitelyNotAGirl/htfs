#pragma once
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <cctype>
namespace url {
	// Helper function to check if a character is safe to leave in the URL
	inline bool isSafeChar(char c) {
	    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
	        return true; // These characters do not need encoding
	    }
	    return false;
	}

	// URL Encode: Encodes special characters in a string into their percent-encoded form
	inline std::string encode(std::string& str) {
	    std::ostringstream encoded;
	    for (char c : str) {
	        if (isSafeChar(c)) {
	            // If the character is safe, add it as is
	            encoded << c;
	        } else {
	            // Otherwise, percent encode it
	            encoded << '%' << std::uppercase << std::hex << int(static_cast<unsigned char>(c));
	        }
	    }
	    return encoded.str();
	}

	// URL Decode: Decodes a percent-encoded string back to its original form
	inline std::string decode(const std::string& str) {
	    std::ostringstream decoded;
	    for (size_t i = 0; i < str.length(); ++i) {
	        if (str[i] == '%' && i + 2 < str.length()) {
	            // If we encounter a percent sign, decode the next two hex digits
	            std::string hex_value = str.substr(i + 1, 2);
	            char decoded_char = static_cast<char>(std::stoi(hex_value, nullptr, 16));
	            decoded << decoded_char;
	            i += 2; // Skip the next two characters (they are part of the encoding)
	        } else {
	            // If it's not a percent-encoded character, add it as is
	            decoded << str[i];
	        }
	    }
	    return decoded.str();
	}
}

inline void StringToAsciiVector(std::string str,std::vector<uint8_t>& vec) {
	vec.resize(str.length());
	memcpy(vec.data(),str.data(),str.length());
}
