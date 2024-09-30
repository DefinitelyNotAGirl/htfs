#pragma once
#include <stdint.hxx>
#include <HttpRequestClient.hxx>
#include <iostream>
#include <string>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>
#include <vector>
#include <thread>
#include <random>
#include <chrono>
#include <openssl/rand.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

#ifdef platform_linux
	static uint64_t htonll(uint64_t value) {
	    if (__BYTE_ORDER == __LITTLE_ENDIAN) {
	        return ((uint64_t)htonl(value & 0xFFFFFFFF) << 32) | htonl(value >> 32);
	    } else {
	        return value;
	    }
	}

	static uint64_t ntohll(uint64_t value) {
	    if (__BYTE_ORDER == __LITTLE_ENDIAN) {
	        return ((uint64_t)ntohl(value & 0xFFFFFFFF) << 32) | ntohl(value >> 32);
	    } else {
	        return value;
	    }
	}
#endif

class WebSocketClient {
	static void defaultTextFrameHandler(std::string& data) {
	}
	static void defaultBinaryFrameHandler(std::vector<u8>& data) {
	}
	static void defaultCloseHandler(std::vector<u8>& data) {
	}
public:
	typedef void(*TextFrameHandler)(std::string& data);
	typedef void(*BinaryFrameHandler)(std::vector<u8>& data);
	typedef void(*CloseHandler)(std::vector<u8>& data);
private:
	static constexpr int BUFFER_SIZE = 4096;
	bool m_isconnected = false;

	TextFrameHandler mTextFrameHandler = &defaultTextFrameHandler;
	BinaryFrameHandler mBinaryFrameHandler = &defaultBinaryFrameHandler;
	CloseHandler mCloseHandler = &defaultCloseHandler;
public:
	void setTextFrameHandler(TextFrameHandler f){mTextFrameHandler = f;}
	void setBinaryFrameHandler(BinaryFrameHandler f){mBinaryFrameHandler = f;}
	void setCloseHandler(CloseHandler f){mCloseHandler = f;}
    WebSocketClient(const std::string& host, const std::string& path, int port = 443)
        : host_(host), path_(path), port_(port), sock_(-1), ssl_ctx_(nullptr), ssl_(nullptr) {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
    }
    ~WebSocketClient() {
        cleanup();
    }
    bool connect() {
        sock_ = create_socket(host_);
        ssl_ctx_ = init_ssl_context();
        ssl_ = create_ssl_connection(sock_, ssl_ctx_);
        if (!perform_handshake()) {
            return false;
        }
        ping_thread_ = std::thread(&WebSocketClient::ping_pong_heartbeat, this);
		m_isconnected = true;
        return true;
    }
	bool IsConnected() {
		return m_isconnected;
	}
    void send_message(const std::string& message) {
        send_frame(message, 0x1); // Text frame opcode
    }
    void send_binary(const std::vector<uint8_t>& data) {
        send_frame(std::string(data.begin(), data.end()), 0x2); // Binary frame opcode
    }
    void close() {
        send_frame("", 0x8); // Close opcode
        SSL_shutdown(ssl_);
		m_isconnected = false;
    }
    void listen() {
        while (true) {
            handle_frame();
        }
    }
private:
    std::string host_;
    std::string path_;
    int port_;
    int sock_;
    SSL_CTX* ssl_ctx_;
    SSL* ssl_;
    std::thread ping_thread_;
    int create_socket(const std::string& host) {
        struct hostent* host_entry = gethostbyname(host.c_str());
        if (!host_entry) {
            handle_errors("Failed to resolve hostname.");
        }
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            handle_errors("Failed to create socket.");
        }
        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port_);
        std::memcpy(&server_addr.sin_addr, host_entry->h_addr_list[0], host_entry->h_length);
        if (::connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            handle_errors("Failed to connect to server.");
        }
        return sock;
    }
    SSL_CTX* init_ssl_context() {
        SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) {
            handle_errors("Failed to create SSL context.");
        }
		// Optional: Force TLS version (e.g., TLS 1.2)
    	SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION); // Minimum TLS 1.2
    	SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION); // Maximum TLS 1.3
		SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
		SSL_CTX_clear_options(ctx, SSL_OP_NO_SSLv3);
		// Optionally set a strong cipher suite
    	SSL_CTX_set_cipher_list(ctx, "TLS_AES_256_GCM_SHA384:TLS_AES_128_GCM_SHA256:TLS_CHACHA20_POLY1305_SHA256");
		SSL_CTX_set_info_callback(ctx, [](const SSL* ssl, int where, int ret) {
		    if (ret == 0) {
		        return; // Error occurred
		    }
		    const char* str;
		    int w = where & ~SSL_ST_MASK;
		    if (w & SSL_ST_CONNECT) {
		        str = "SSL_connect";
		    } else if (w & SSL_ST_ACCEPT) {
		        str = "SSL_accept";
		    } else {
		        str = "undefined";
		    }
		    if (where & SSL_CB_LOOP) {
		        printf("%s: %s\n", str, SSL_state_string_long(ssl));
		    } else if (where & SSL_CB_ALERT) {
		        str = (where & SSL_CB_READ) ? "read" : "write";
		        printf("SSL3 alert %s: %s\n", str, SSL_alert_desc_string_long(ret));
		    } else if (where & SSL_CB_EXIT) {
		        if (ret == 0) {
		            printf("%s: failed in %s\n", str, SSL_state_string_long(ssl));
		        } else if (ret < 0) {
		            printf("%s: error in %s\n", str, SSL_state_string_long(ssl));
		        }
		    }
		});
        return ctx;
    }
    SSL* create_ssl_connection(int sock, SSL_CTX* ctx) {
        SSL* ssl = SSL_new(ctx);
		int version = SSL_version(ssl);
		std::cout << "Protocol version: " << SSL_get_version(ssl) << std::endl;
        if (!ssl) {
            handle_errors("Failed to create SSL structure.");
        }
        SSL_set_fd(ssl, sock);
        if (SSL_connect(ssl) <= 0) {
            handle_errors("Failed to establish SSL connection.");
			// Log SSL error codes
    		unsigned long err;
    		while ((err = ERR_get_error()) != 0) {
    		    char *err_str = ERR_error_string(err, NULL);
    		    std::cerr << "SSL error: " << err_str << "\n";
    		}
        }
        return ssl;
    }
	// Function to generate a random Base64-encoded key
	std::string generate_random_key() {
	    unsigned char random_bytes[16]; // 16 bytes for the key
	    if (RAND_bytes(random_bytes, sizeof(random_bytes)) != 1) {
	        handle_errors("Failed to generate random bytes for WebSocket key.");
	    }
	    // Base64 encode the random bytes
	    BIO* bio;
	    BUF_MEM* buffer_ptr;
	    bio = BIO_new(BIO_s_mem());
	    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL); // No newlines
	    BIO_write(bio, random_bytes, sizeof(random_bytes));
	    BIO_flush(bio);
	    BIO_get_mem_ptr(bio, &buffer_ptr);
	    std::string key(buffer_ptr->data, buffer_ptr->length);
	    BIO_free(bio);
	    return key;
	}
	std::string generate_websocket_handshake() {
	    std::string key = generate_random_key();
	    std::string handshake = "GET " + path_ + " HTTP/1.1\r\n";
	    handshake += "Host: " + host_ + "\r\n";
	    handshake += "Upgrade: websocket\r\n";
	    handshake += "Connection: Upgrade\r\n";
	    handshake += "Sec-WebSocket-Key: " + key + "\r\n"; // Use the generated key
	    handshake += "Sec-WebSocket-Version: 13\r\n";
	    handshake += "\r\n";
	    return handshake;
	}
    bool perform_handshake() {
        std::string handshake = generate_websocket_handshake();
        if (SSL_write(ssl_, handshake.c_str(), handshake.length()) <= 0) {
            handle_errors("Failed to send WebSocket handshake.");
            return false;
        }
        char buffer[BUFFER_SIZE];
        int bytes_received = SSL_read(ssl_, buffer, sizeof(buffer) - 1);
        if (bytes_received <= 0) {
            handle_errors("Failed to read handshake response.");
            return false;
        }
        buffer[bytes_received] = '\0';
        std::cout << "Handshake Response:\n" << buffer << "\n";
        return true;
    }
    void send_frame(const std::string& payload, uint8_t opcode) {
        std::vector<uint8_t> frame;
        frame.push_back(0x80 | opcode); // FIN bit set, opcode
        if (payload.length() <= 125) {
            frame.push_back(static_cast<uint8_t>(payload.length()) | 0x80); // Masked
        } else if (payload.length() <= 65535) {
            frame.push_back(126 | 0x80); // Masked
            uint16_t len = htons(static_cast<uint16_t>(payload.length()));
            frame.insert(frame.end(), reinterpret_cast<uint8_t*>(&len), reinterpret_cast<uint8_t*>(&len) + sizeof(len));
        } else {
            frame.push_back(127 | 0x80); // Masked
            uint64_t len = htonll(static_cast<uint64_t>(payload.length())); // Use our own htonll function
            frame.insert(frame.end(), reinterpret_cast<uint8_t*>(&len), reinterpret_cast<uint8_t*>(&len) + sizeof(len));
        }
        std::vector<uint8_t> mask_key = generate_masking_key();
        frame.insert(frame.end(), mask_key.begin(), mask_key.end());
        std::vector<uint8_t> masked_payload(payload.begin(), payload.end());
        mask_payload(masked_payload, mask_key);
        frame.insert(frame.end(), masked_payload.begin(), masked_payload.end());
        if (SSL_write(ssl_, frame.data(), frame.size()) <= 0) {
            handle_errors("Failed to send WebSocket frame.");
        }
    }
    void handle_frame() {
        uint8_t header[2];
        if (SSL_read(ssl_, header, 2) <= 0) {
            handle_errors("Failed to read WebSocket frame header.");
        }
        uint8_t opcode = header[0] & 0x0F;
        uint8_t payload_len = header[1] & 0x7F;
        size_t data_len = payload_len;
        if (payload_len == 126) {
            uint16_t extended_len;
            if (SSL_read(ssl_, &extended_len, sizeof(extended_len)) <= 0) {
                handle_errors("Failed to read extended payload length.");
            }
            data_len = ntohs(extended_len);
        } else if (payload_len == 127) {
            uint64_t extended_len;
            if (SSL_read(ssl_, &extended_len, sizeof(extended_len)) <= 0) {
                handle_errors("Failed to read extended payload length.");
            }
            data_len = ntohl(extended_len >> 32) | static_cast<uint64_t>(ntohl(static_cast<uint32_t>(extended_len))) << 32; // Manual conversion
        }
        std::vector<uint8_t> payload(data_len);
        if (SSL_read(ssl_, payload.data(), data_len) <= 0) {
            handle_errors("Failed to read WebSocket frame payload.");
        }
        switch (opcode) {
            case 0x1: // Text frame
                std::cout << "Received message: " << std::string(payload.begin(), payload.end()) << "\n";
				{
					std::string data(payload.begin(), payload.end());
					mTextFrameHandler(data);
				}
                break;
            case 0x2: // Binary frame
                std::cout << "Received binary data of length: " << payload.size() << "\n";
				mBinaryFrameHandler(payload);
                break;
            case 0x9: // Ping frame
                handle_ping(payload);
                break;
            case 0xA: // Pong frame
                std::cout << "Received Pong!\n";
                break;
            case 0x8: // Close frame
                handle_close(payload);
				mCloseHandler(payload);
                break;
            default:
                std::cout << "Unhandled frame type: " << static_cast<int>(opcode) << "\n";
                break;
        }
    }
    void handle_ping(const std::vector<uint8_t>& payload) {
        std::cout << "Received Ping, sending Pong...\n";
        send_frame(std::string(payload.begin(), payload.end()), 0xA); // Pong opcode
    }
    void handle_close(const std::vector<uint8_t>& payload) {
        std::cout << "Received Close frame, closing connection...\n";
		std::vector<u8> ncPayload(payload);
		mCloseHandler(ncPayload);
        close();
    }
    std::vector<uint8_t> generate_masking_key() {
        std::vector<uint8_t> mask_key(4);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint8_t> dis(0, 255);
        for (int i = 0; i < 4; ++i) {
            mask_key[i] = dis(gen);
        }
        return mask_key;
    }
    void mask_payload(std::vector<uint8_t>& payload, const std::vector<uint8_t>& mask_key) {
        for (size_t i = 0; i < payload.size(); ++i) {
            payload[i] ^= mask_key[i % 4];
        }
    }
    void ping_pong_heartbeat() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            send_frame("", 0x9); // Ping opcode
        }
    }
    void cleanup() {
        if (ssl_) {
            SSL_shutdown(ssl_);
            SSL_free(ssl_);
        }
        if (sock_ != -1) {
            ::close(sock_);
			m_isconnected = false;
        }
        if (ssl_ctx_) {
            SSL_CTX_free(ssl_ctx_);
        }
        EVP_cleanup();
        if (ping_thread_.joinable()) {
            ping_thread_.join();
        }
    }
    void handle_errors(const std::string& msg) {
        std::cerr << msg << "\n";
        ERR_print_errors_fp(stderr);
    }
};
