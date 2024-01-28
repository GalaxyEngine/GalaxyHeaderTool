#include <iostream>

#include "HeaderTool.h"

using namespace std;

int main(int argc, char** argv)
{
#ifndef DEBUG

	if (argc <= 1) {
		std::cerr << "Error: no argument" << std::endl;
		return 0;
	}

	HeaderTool headerTool;
	headerTool.SetGeneratedFolder(std::string(argv[2]));
	headerTool.ParseFiles(argv[1]);

#else

	HeaderTool headerTool;
	headerTool.SetGeneratedFolder("D:/Code/Moteurs/ExampleProject/Generate/Headers");
	headerTool.ParseFiles("D:/Code/Moteurs/ExampleProject");
#endif
}
