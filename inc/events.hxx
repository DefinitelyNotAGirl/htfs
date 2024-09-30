#pragma once
#include <stdint.hxx>
#include <HttpRequestClient.hxx>

namespace events {
	namespace Twitch {
		void chat(u64 BroadcasterID,u64 UserID,HttpRequestClient& Client);
	}
}
