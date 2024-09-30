#pragma once
#include "stdint.hxx"
#include <string>

#ifndef HTTP_ACCESS
	#define HTTP_ACCESS private
#endif

enum class Platform {
	Youtube,
	Twitch,
	Tiktok
};

class MultiplatformObject {
HTTP_ACCESS:
	Platform m_platform;
public:
	Platform GetPlatform() {return this->m_platform;}
	std::string GetPlatformString() {
		switch(this->m_platform) {
			case(Platform::Youtube): return "YouTube";
			case(Platform::Twitch): return "Twitch";
			case(Platform::Tiktok): return "TikTok";
			default:
				return "UNKNOWN PLATFORM";
		}
	}
};

class User : public MultiplatformObject {
HTTP_ACCESS:
	std::string Displayname;
public:
	class Banned{};
	virtual void Timeout(u64 seconds){
		throw std::runtime_error("::User::Timeout not overwritten");
	}
	virtual void Ban() {
		throw std::runtime_error("::User::Ban not overwritten");
	}
	virtual bool IsModerator(){return false;}
	virtual bool HasLinkPermission(){return false;};
	virtual bool HasBadMessagePermission(){return false;};
	virtual std::string& GetDisplayname(){return this->Displayname;}
	virtual u64 GetMessageCount(){return uint64_t_max;}
};

class Message : public MultiplatformObject {
HTTP_ACCESS:
	std::string m_text;
	User& user;
public:
	class Deletion{};
	Message(User& user): user(user){}
	virtual std::string& GetText() {return this->m_text;}
	virtual User& GetUser(){return this->user;}
	virtual void Delete(){throw Deletion();};
	virtual void SendReply(std::string text){};
};
