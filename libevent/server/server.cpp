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
using namespace std;

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
			cout << "this is test module" << endl;
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
		}
		void set_charid(int charid){m_charid = charid;}
		int get_charid(){return m_charid;}
		intptr_t get_fd(){return m_fd;}
		char* get_remoteaddr() {return inet_ntoa(m_sin.sin_addr);}
		libevent_server_module* get_module(){return m_module;}
		bufferevent* get_bufferevent(){return m_bev;}

	private:
		int m_charid;
		string m_buf;
		intptr_t m_fd;
		sockaddr_in m_sin;
		libevent_server_module *m_module;
		bufferevent *m_bev;

};
//--------------------------------------------
class server_module: public net_module
{
	public:
		server_module() {modname = "server_module";}
};
//--------------------------------------------
class libevent_server_module: public server_module
{
	private:
		struct event_base *base;
		struct evconnlistener *listener;
		std::map<evutil_socket_t, net_object *> m_objects;
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
		net_object* get_net_object(intptr_t fd)
		{
			auto it = m_objects.find(fd);
			if(it != m_objects.end())
			{
				return it->second;
			}
			return NULL;
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

			bufferevent_setcb(bev, cb_read, NULL, cb_event, (void*)object);
			bufferevent_enable(bev, EV_READ|EV_WRITE);
			module->add_net_object(fd, object);
		
			cb_event(bev, BEV_EVENT_CONNECTED, (void*)object);

			struct timeval tv;
			tv.tv_sec = 10;
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
				cout << "client " << object->get_fd() << " send " << buf << endl;
				
				string sendbuf(buf); 
				object->get_module()->sendmsg(object->get_fd(), sendbuf);
				delete buf;
			}
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
				cout << "Client Disconnected " << object->get_fd() << " " << object->get_remoteaddr() << endl;
				object->get_module()->del_net_object(object->get_fd());		
			}
			else if(events & BEV_EVENT_CONNECTED)
			{
				cout << "Client Connected " << object->get_fd() << " " << object->get_remoteaddr() << endl;
			}
		}
		void sendmsg(intptr_t fd, string msg)
		{
			net_object *object = get_net_object(fd);
			if(!object)
			{
				return;
			}
			bufferevent *bev = object->get_bufferevent();
			if(NULL != bev)
			{
				bufferevent_write(bev, msg.data(), msg.length());
			}
		}
};
//--------------------------------------------


//--------------------------------------------
// server
//--------------------------------------------
class server
{
	private:
		std::vector<module *> modules;
		void _addmodule(module *m) {modules.push_back(m);}

		bool is_running;

	public:
		bool init()
		{
			_addmodule(new libevent_server_module());
			_addmodule(new test_module());

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
			int fps = 3;
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
