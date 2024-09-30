#pragma once
#include <stdint.hxx>
#include <HttpRequestClient.hxx>
#include <Websocket.hxx>
#include <util.hxx>
#include <JSON.hxx>
#include <async.hxx>

namespace events {
	namespace __private {
	}
	namespace Twitch {
		std::string SessionID;
		WebSocketClient real_websocket("eventsub.wss.twitch.tv","/ws");
		WebSocketClient& GetWebsocket() {
			real_websocket.setTextFrameHandler([](std::string& data){
				JSON body(nullptr,data);
				std::cout << "data: " << body.toString("") << std::endl;
			});
			return real_websocket;
		}
		WebSocketClient& websocket = GetWebsocket();
		void EnsureConnected() {
			if(!websocket.IsConnected()) {
				std::cout << "connecting to twitch EventSub server..." << std::endl;
				if(websocket.connect()) {
					std::cout << "[OK] connected to twitch EventSub server" << std::endl;
					async([](void*){
						websocket.listen();
					});
				} else {
					throw std::runtime_error("websocket failed to connect.");
				}
			}
		}
		typedef void(*EventListenerRegister)(void*);
		std::vector<std::pair<EventListenerRegister,void*>> RegistrationBacklog;

		void chat(u64 BroadcasterID,u64 UserID,HttpRequestClient& Client) {
			EnsureConnected();
			struct input {
				u64 BroadcasterID;
				u64 UserID;
				HttpRequestClient& Client;
				input(u64 BroadcasterID,u64 UserID,HttpRequestClient& Client): BroadcasterID(BroadcasterID),UserID(UserID),Client(Client) {}
			};
			input* i = new input(BroadcasterID,UserID,Client);
			auto f = [](void* i){
				HttpRequest request;
				request.url = "https://api.twitch.tv/helix/eventsub/subscriptions";
				request.method = "POST";
				StringToAsciiVector(JSON::object({
					JSON("type","channel.chat.message"),
					JSON("version",(i64)1),
					JSON::object("condition",{
						JSON("broadcaster_user_id",std::to_string(((input*)i)->BroadcasterID)),
						JSON("user_id",std::to_string(((input*)i)->UserID))
					}),
					JSON::object("transport",{
						JSON("method","websocket"),
						JSON("session_id",SessionID)
					})
				}).toString(""),request.body);
				((input*)i)->Client.Send(request);
				delete (input*)i;
			};
			return SessionID.empty() ? RegistrationBacklog.push_back({f,(void*)i}) : f(i);
		}
	}
}
