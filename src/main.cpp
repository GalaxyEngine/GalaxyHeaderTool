#include <iostream>

#include "HeaderTool.h"

using namespace std;

int main(int argc, char** argv)
{

	//throw std::runtime_error("An error occurred!");
#ifndef DEBUG
	//TODO : Add ENUM, STRUCT, FUNCTION() argument 
	if (argc <= 1) {
		std::cerr << "Error: no argument" << std::endl;
		return 1;
	}

	HeaderTool headerTool;
	headerTool.SetGeneratedFolder(std::string(argv[2]));
	headerTool.ParseFiles(argv[1]);

#else

	HeaderTool headerTool;
	headerTool.SetGeneratedFolder("D:/Code/Moteurs/Galaxy Projects/GalaxyProject/Generate/Headers");
	headerTool.ParseFiles("D:/Code/Moteurs/Galaxy Projects/GalaxyProject");
#endif
	return 0;
}