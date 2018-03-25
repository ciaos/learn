#include "headers.h"
#include "luaheader.h"
#include "libevent_server.h"
#include "libevent_client.h"

static int write(lua_State *L)
{
	int n = lua_gettop(L);
	if(n != 2)
	{
		lua_pushnumber(L, -1);
		return 1;
	}
	intptr_t fd = lua_tonumber(L, 1);
	size_t len = 0;
	const char* msg = luaL_checklstring(L, 2, &len);
	auto it = m_objects.find(fd);
	if(it != m_objects.end())
	{
		it->second->send(msg, len);
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
		bool _initvm(string luapath)
		{
			L = luaL_newstate();
			if(L == NULL)
			{
				return false;
			}
			luaL_openlibs(L);
			lua_register(L, "cwrite", write);
			int ret = luaL_dofile(L, luapath.c_str());
			cout << luapath.c_str() << endl;
			if(ret)
			{
				cerr << "luaL_dofile" << ret << endl;
				return false;
			}
			return true;
		}
	public:
		bool init(string luapath)
		{
			if(!_initvm(luapath))
			{
				cerr << "initvm error" << endl;
				return false;
			}

			_addmodule(new libevent_server_module());
//			_addmodule(new libevent_client_module());

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
