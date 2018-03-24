#include <iostream>
#include <vector>
#include <csignal>
#include <boost/chrono.hpp>
#include <boost/thread.hpp>
#include <event2/event.h>
#include <event2/listener.h>
#include <netinet/tcp.h>
#include <event2/bufferevent.h>
#include <arpa/inet.h>
#include <event2/buffer.h>
extern "C"  
{  
#include "lua.h"  
#include "lauxlib.h"  
#include "lualib.h"  
} 
using namespace std;

lua_State *L;
//--------------------------------------------
// module
//--------------------------------------------
class module
{
	public:
		module(){}
		~module(){}

		virtual bool init() {cout << "init " << modname << endl;return true;}
		virtual void loop() {}
		virtual void shut() {cout << "shut " << modname << endl;}

	protected:
		string modname;
};

//--------------------------------------------
class net_module : public module
{
	public:
		net_module() {}
		virtual void onConnected() {}
		virtual void onRecv() {}
		virtual void onError() {}
};

//--------------------------------------------
class test_module : public module
{
	public:
		test_module() {modname = "test_module";}
		void loop()
		{
			//cout << "this is test module" << endl;
		}
};
//--------------------------------------------
class libevent_server_module;
class net_object
{
	public:
		net_object(intptr_t fd, sockaddr_in& addr, libevent_server_module* module, bufferevent *bev)
		{
			m_fd = fd;
			memset(&m_sin, 0, sizeof(m_sin));
			m_sin = addr;
			m_module = module;
			m_bev = bev;

			m_sendbuffer = evbuffer_new();
		}
		void set_charid(int charid){m_charid = charid;}
		int get_charid(){return m_charid;}
		intptr_t get_fd(){return m_fd;}
		char* get_remoteaddr() {return inet_ntoa(m_sin.sin_addr);}
		libevent_server_module* get_module(){return m_module;}
		bufferevent* get_bufferevent(){return m_bev;}
		evbuffer* get_sendbuffer(){return m_sendbuffer;}

		void send(string msgdata)
		{
			cout << "sendto client " << msgdata.size() << endl;
			if(msgdata != "")
			{
				evbuffer_add(m_sendbuffer, msgdata.c_str(), msgdata.size());
			}
			int len = evbuffer_get_length(m_sendbuffer);
			if(len > 0)
			{
				int ret = bufferevent_write_buffer(m_bev, m_sendbuffer);
				if(ret)
				{
					bufferevent_enable(m_bev, EV_WRITE);
				}
			}
		}
	private:
		int m_charid;
		string m_buf;
		intptr_t m_fd;
		sockaddr_in m_sin;
		libevent_server_module *m_module;
		bufferevent *m_bev;
		evbuffer* m_sendbuffer;
};
//--------------------------------------------
class server_module: public net_module
{
	public:
		server_module() {modname = "server_module";}
};
//--------------------------------------------
std::map<evutil_socket_t, net_object *> m_objects;
class libevent_server_module: public server_module
{
	private:
		struct event_base *base;
		struct evconnlistener *listener;
	public:
		libevent_server_module()
		{
			base = NULL;
			listener = NULL;
		}
		bool init() override final
		{
			base = event_base_new();
			if(!base)
			{
				return false;
			}
			struct sockaddr_in sin;
			memset(&sin, 0, sizeof(sin));
			sin.sin_family = AF_INET;
			sin.sin_addr.s_addr = 0;
			sin.sin_port = htons(3000);
			listener = evconnlistener_new_bind(base, cb_accept, (void *)this, LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1, (struct sockaddr *)&sin, sizeof(sin));
			if(!listener)
			{
				return false;
			}
			return server_module::init();
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
				lua_pushstring(L, buf);
				int ret = lua_pcall(L, 2, 0, 0);
				if(ret)
				{
					cerr << "panic" <<  ret << endl;
				}

				string sendbuf(buf); 
				object->send(sendbuf);
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
			object->send("");
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
				lua_pushstring(L, object->get_remoteaddr());
				int ret = lua_pcall(L, 2, 0, 0);
				if(ret)
				{
					cerr << "panic" << endl;
				}
			}
		}
};
//--------------------------------------------
class libevent_client_module;
class connect_object
{
	private:
		bufferevent* m_bev;
		intptr_t m_fd;
		evbuffer* m_sendbuffer;

		string m_proxy_addr;
		short m_proxy_port;
		libevent_client_module* m_module;

	public:
		connect_object()
		{
			m_proxy_addr = "127.0.0.1";
			m_proxy_port = 3000;
			m_sendbuffer = evbuffer_new();
		}
		void init(bufferevent* bev, intptr_t fd, libevent_client_module* module)
		{
			m_bev = bev;
			m_fd = fd;
			m_module = module;
		}
		void send(string msgdata)
		{
			if(msgdata != "")
			{
				evbuffer_add(m_sendbuffer, msgdata.c_str(), msgdata.size());
			}
			int len = evbuffer_get_length(m_sendbuffer);
			if(len > 0)
			{
				int ret = bufferevent_write_buffer(m_bev, m_sendbuffer);
				if(ret)
				{
					bufferevent_enable(m_bev, EV_WRITE);
				}
			}
		}
		bufferevent* get_bufferevent(){return m_bev;}
		intptr_t get_fd(){return m_fd;}
		string get_proxy_addr(){return m_proxy_addr;}
		short get_proxy_port(){return m_proxy_port;}
		evbuffer* get_sendbuffer(){return m_sendbuffer;}
		libevent_client_module* get_module(){return m_module;}
};
//--------------------------------------------
class client_module: public net_module
{
	public:
		client_module() {modname = "client_module";}
};
//--------------------------------------------
class libevent_client_module: public client_module
{
	private:
		struct event_base *base;
		connect_object connect;
		bool async_connect_proxy(connect_object &connect)
		{
			struct bufferevent* bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
			if(!bev)
			{
				cerr << "bufferevent_socket_new" << endl;
				return false;
			}
			struct sockaddr_in sin;
			memset(&sin, 0, sizeof(sin));
			sin.sin_family = AF_INET;
			sin.sin_port = htons(connect.get_proxy_port());
			int ret = evutil_inet_pton(AF_INET, connect.get_proxy_addr().c_str(), &sin.sin_addr);
			if(ret != 1) {return false;}

			bufferevent_setcb(bev, cb_read, cb_write, cb_event, (void *)&connect);
			//bufferevent_setwatermark(bev, EV_WRITE, 1, 0);
			bufferevent_enable(bev, EV_READ|EV_WRITE);
			if(bufferevent_socket_connect(bev, (struct sockaddr *)&sin, sizeof(sin)) < 0)
			{
				cerr << "bufferevent_socket_connect" << endl;
				bufferevent_free(bev);
				return false;
			}
			intptr_t fd = bufferevent_getfd(bev);
			if(fd == -1)
			{
				cerr << "bufferevent_getfd" << endl;
				bufferevent_free(bev);
				return false;
			}
			connect.init(bev, fd, this);

			struct timeval tv;
			tv.tv_sec = 10;
			tv.tv_usec = 0;
			bufferevent_set_timeouts(bev, &tv, NULL);
			return true;
		}
		static void cb_read(struct bufferevent *bev, void *user_data)
		{
			evbuffer* input = bufferevent_get_input(bev);
			if(!input)
			{
				return;
			}
			size_t len = evbuffer_get_length(input);
			if(len <= 0)
			{
				return;
			}
			char* buf = new char[len];
			evbuffer_remove(input, buf, len);
			cout << "client get " << buf << endl;
			delete buf;
		}
		static void cb_write(struct bufferevent *bev, void *user_data)
		{
			connect_object *object = (connect_object *)user_data;
			if(!object)
			{
				cerr << "connect object null" << endl;
				return;
			}
			object->send("");
		}

		static void cb_event(struct bufferevent *bev, short events, void *user_data)
		{
			connect_object *object = (connect_object *)user_data;
			if(!object)
			{
				cerr << "connect object null" << endl;
				return;
			}

			if(events & BEV_EVENT_EOF || events & BEV_EVENT_ERROR || events & BEV_EVENT_TIMEOUT)
			{
			}
			else if(events & BEV_EVENT_CONNECTED)
			{
				object->send("helloaaa");
			}
		}
		static void cb_timeout(evutil_socket_t fd, short, void *ctx)
		{
			cout << "timeout " << endl;
		}
	public:
		bool init() override final
		{
			base = event_base_new();
			if(!base)
			{
				return false;
			}

			if(!async_connect_proxy(connect))
			{
				return false;
			}

			struct timeval tv;
			tv.tv_sec = 3;
			tv.tv_usec = 0;
			struct event* ev = event_new(base, -1, EV_TIMEOUT|EV_WRITE|EV_PERSIST, cb_timeout, (void *)&connect);
			event_add(ev, &tv);
			return client_module::init();
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
};

//--------------------------------------------
// server
//--------------------------------------------
static int write(lua_State *L)
{
	int n = lua_gettop(L);
	if(n != 2)
	{
		lua_pushnumber(L, -1);
		return 1;
	}
	intptr_t fd = lua_tonumber(L, 1);
	string msg = lua_tostring(L, 2);
	auto it = m_objects.find(fd);
	if(it != m_objects.end())
	{
		it->second->send(msg);
		lua_pushnumber(L, 0);
		return 1;
	}

	lua_pushnumber(L, -2);
	return 1;
}
class server
{
	private:
		std::vector<module *> modules;
		void _addmodule(module *m) {modules.push_back(m);}

		bool is_running;
		bool _initvm()
		{
			L = luaL_newstate();
			if(L == NULL)
			{
				return false;
			}
			luaL_openlibs(L);
			lua_register(L, "cwrite", write);
			int ret = luaL_dofile(L, "main.lua");
			if(ret)
			{
				cerr << "luaL_dofile" << endl;
				return false;
			}
			return true;
		}
	public:

		bool init()
		{
			if(!_initvm())
			{
				cerr << "initvm error" << endl;
				return false;
			}

			_addmodule(new libevent_server_module());
			_addmodule(new test_module());
			//_addmodule(new libevent_client_module());

			for(auto it = modules.begin(); it != modules.end(); it ++)
			{
				if((*it) && (*it)->init() == false)
				{
					return false;
				}
			}

			is_running = true;
			return true;
		}

		void run()
		{
			int fps = 30;
			while(is_running)
			{
				int total = 0;
				auto begin = boost::chrono::high_resolution_clock::now();
				for(int i=0; i < fps; i ++)
				{
					for(auto it = modules.begin(); it != modules.end(); it ++)
					{
						if(*it)
						{
							(*it)->loop();
						}
					}
					auto cost = boost::chrono::high_resolution_clock::now() - begin;
					total = boost::chrono::duration_cast<boost::chrono::milliseconds>(cost).count();
					if(total > 1000) {break;}
					if(total < 1000 / fps * (i + 1) )
					{
						boost::this_thread::sleep_for(boost::chrono::milliseconds(1000/fps*(i+1)- total));
					}
				}
			}
			_shut();
		}
		void stop()
		{
			is_running = false;
		}
	private:
		void _shut()
		{
			for(auto it = modules.begin(); it != modules.end(); it ++)
			{
				if(*it)
				{
					(*it)->shut();
				}
			}
			lua_close(L);
		}
};

//--------------------------------------------
// main
//--------------------------------------------
server s;

void stopserver(int signal)
{
	s.stop();
}

int main()
{
	try
	{
		cout << "server start ..." << endl;
		signal(SIGINT, stopserver);

		if(!s.init())
		{
			cerr << "server start failed" << endl;
			return -1;
		}
		cout << "server start ok" << endl;
		s.run();
	}
	catch(std::exception &e)
	{
		cerr << e.what() << endl;
	}
	return 0;
}
