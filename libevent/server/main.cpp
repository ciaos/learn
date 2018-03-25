#include "src/headers.h"
#include "src/module.h"
#include "src/netobject.h"
#include "src/luaheader.h"
#include "src/server.h"

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
		signal(SIGINT, stopserver);

		if(!s.init("lua/main.lua"))
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
