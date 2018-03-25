#include "headers.h"
#include "luaheader.h"

lua_State *L;
//--------------------------------------------
std::map<evutil_socket_t, net_object *> m_objects;

class libevent_server_module: public module
{
	private:
		struct event_base *base;
		struct evconnlistener *listener;
	public:
		libevent_server_module()
		{
			modname = "server_module";
			base = NULL;
			listener = NULL;
		}
		bool init() override final
		{
			char *host = NULL;
			short port = 0;
			lua_getglobal(L, "host");
			if (lua_isstring(L, -1))
			{
				host = (char *)lua_tostring(L, -1);
			}
			lua_pop(L, -1);
			lua_getglobal(L, "port");
			if (lua_isnumber(L, -1))
			{
				port = (short)lua_tonumber(L, -1);
			}
			lua_pop(L, -1);
			if(host == NULL || port == 0)
			{
				cerr << "invalid host or port config" << endl;
				return false;
			}

			base = event_base_new();
			if(!base)
			{
				return false;
			}

			struct sockaddr_in sin;
			memset(&sin, 0, sizeof(sin));
			sin.sin_family = AF_INET;
			sin.sin_addr.s_addr = inet_addr(host);
			sin.sin_port = htons(port);
			listener = evconnlistener_new_bind(base, cb_accept, (void *)this, LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1, (struct sockaddr *)&sin, sizeof(sin));
			if(!listener)
			{
				return false;
			}
			cout << "server start on " << host << ":" << port << endl;
			return module::init();
		}
		void loop() override final
		{
			if(base)
			{
				event_base_loop(base, EVLOOP_ONCE|EVLOOP_NONBLOCK);
			}
		}
		void shut() override final
		{
			if(base)
			{
				event_base_free(base);
			}
		}
		void add_net_object(intptr_t fd, net_object *object)
		{
			m_objects.insert(make_pair(fd, object));
		}
		void del_net_object(intptr_t fd)
		{
			auto it = m_objects.find(fd);
			if(it != m_objects.end())
			{
				net_object *object = it->second;
				m_objects.erase(it);
				delete object;
				object = NULL;
			}
		}

		static void cb_accept(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *sa, int socklen, void *user_data)
		{
			libevent_server_module *module = (libevent_server_module *)user_data;
			if(!module)
			{
				cerr << "libevent_server_module null" << endl;
				return;
			}

			int enable = 1;
			if(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void*)&enable, sizeof(enable)) < 0)
			{
				cerr << "setsockopt tcpnodelay " << strerror(errno) << endl;
				return;
			}
			struct linger ling = {0, 0};
			if(setsockopt(fd, SOL_SOCKET, SO_LINGER, (char *)&ling, sizeof(linger)) == -1)
			{
				cerr << "setsockopt so_linger " << strerror(errno) << endl;
				return;
			}

			struct event_base *base = evconnlistener_get_base(listener);
			struct bufferevent *bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
			if(!bev)
			{
				cerr << "bufferevent alloc " << strerror(errno) << endl;
				return;
			}
			struct sockaddr_in* psin = (sockaddr_in*)sa;
			net_object* object = new net_object(fd, *psin, module, bev);
			if(!object)
			{
				cerr << "net_object alloc error " << endl;
				return;
			}

			bufferevent_setcb(bev, cb_read, cb_write, cb_event, (void*)object);
			bufferevent_enable(bev, EV_READ|EV_WRITE);
			module->add_net_object(fd, object);

			cb_event(bev, BEV_EVENT_CONNECTED, (void*)object);

			struct timeval tv;
			tv.tv_sec = 120;
			tv.tv_usec = 0;
			bufferevent_set_timeouts(bev, &tv, NULL);
		}

		static void cb_read(struct bufferevent *bev, void *user_data)
		{
			net_object *object = (net_object *)user_data;
			if(!object)
			{
				cerr << "object null" << endl;
				return;
			}

			struct evbuffer *input = bufferevent_get_input(bev);
			if(!input)
			{
				return;
			}

			while(1)
			{
				size_t len = evbuffer_get_length(input);
				if(len == 0)
				{
					return;
				}

				char* buf = new char[len];
				evbuffer_remove(input, buf, len);
				lua_getglobal(L, "onRecv");
				lua_pushnumber(L, object->get_fd());
				lua_pushlstring(L, buf, len);
				int ret = lua_pcall(L, 2, 0, 0);
				if(ret)
				{
					cerr << "panic" <<  ret << endl;
				}
				delete buf;
			}
		}
		static void cb_write(struct bufferevent *bev, void *user_data)
		{
			net_object *object = (net_object *)user_data;
			if(!object)
			{
				cerr << "connect object null" << endl;
				return;
			}
			object->send(NULL, 0);
		}
		static void cb_event(struct bufferevent *bev, short events, void *user_data)
		{
			net_object *object = (net_object *)user_data;
			if(!object)
			{
				cerr << "object null" << endl;
				return;
			}

			if(events & BEV_EVENT_EOF || events & BEV_EVENT_ERROR || events & BEV_EVENT_TIMEOUT)
			{
				lua_getglobal(L, "onError");
				lua_pushnumber(L, object->get_fd());
				lua_pushnumber(L, events);
				int ret = lua_pcall(L, 2, 0, 0);
				if(ret)
				{
					cerr << "panic" << endl;
				}
				object->get_module()->del_net_object(object->get_fd());		
			}
			else if(events & BEV_EVENT_CONNECTED)
			{
				//lua_pcall(L, 0, 0, 0);
				lua_getglobal(L, "onConnected");
				lua_pushnumber(L, object->get_fd());
				char addr[32] = {0};
				object->get_remoteaddr(addr);
				lua_pushstring(L, addr);
				int ret = lua_pcall(L, 2, 0, 0);
				if(ret)
				{
					cerr << "panic" << endl;
				}
			}
		}
};
