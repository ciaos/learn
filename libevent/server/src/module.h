#include"headers.h"

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
