#include"headers.h"
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
		void get_remoteaddr(char* buf)
		{
			char *addr = inet_ntoa(m_sin.sin_addr);
			short port = m_sin.sin_port;
			sprintf(buf, "%s:%d", addr, port);
		}
		libevent_server_module* get_module(){return m_module;}
		bufferevent* get_bufferevent(){return m_bev;}
		evbuffer* get_sendbuffer(){return m_sendbuffer;}

		void send(const char * msgdata, size_t msglen)
		{
			if(msglen != 0)
			{
				evbuffer_add(m_sendbuffer, msgdata, msglen);
			}
			int len = evbuffer_get_length(m_sendbuffer);
			if(len > 0)
			{
				bufferevent_write_buffer(m_bev, m_sendbuffer);
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
extern std::map<evutil_socket_t, net_object *> m_objects;
