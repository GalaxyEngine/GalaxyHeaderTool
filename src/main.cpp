#include <iostream>

#include "HeaderTool.h"

using namespace std;

int main(int argc, char** argv)
{

	//throw std::runtime_error("An error occurred!");
#ifndef DEBUG
	//TODO : Add ENUM, STRUCT, CLASS (to check if need to generate a file) defines

	/* Enum e.g :
	// File
	ENUM()
	enum EType
	{
		Float,
		Int,
		Double
	};
	
	// Generated file
	const char* Get_EType_String(int index)
	{
		switch (index)
		{
		case 0:
			return "Float";
		case 1:
			return "Int";
		case 2:
			return "Double";
		default:
			return "Invalid";
		}
	}
	*/
	if (argc <= 1) {
		std::cerr << "Error: no argument" << std::endl;
		return 1;
	}

	HeaderTool headerTool;
	headerTool.SetGeneratedFolder(std::string(argv[2]));
	headerTool.ParseFiles(argv[1]);

#else

	HeaderTool headerTool;
	headerTool.SetGeneratedFolder("D:/Code/Moteurs/ExampleProject/Generate/Headers");
	headerTool.ParseFiles("D:/Code/Moteurs/ExampleProject");
#endif
	return 0;
}