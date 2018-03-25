#include "headers.h"

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
class libevent_client_module: public module
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
};
