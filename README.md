# GalaxyHeaderTool
## Overview

GalaxyHeaderTool is a software that will parse a c++ header file and will generated header file and file that will be parsed to make the [GalaxyScript](https://github.com/GalaxyEngine/GalaxyScript) Library Work

## How to build it yourself
You need to use xmake to build it yourself :
```bash
xmake f -p [macosx|linux|iphoneos ..] -a [x86_64|i386|arm64 ..] -m [debug|release]
```
## How to use it
You need to give two arguments to the software : 
- the first one is the folder where all the includes of the project that need to have generated header files, 
- the second one is the path where the generated files should be put

There is an example in the [GalaxyScript](https://github.com/GalaxyEngine/GalaxyScript) Repository.

So basicaly before generation of the Project DLL, you should run this software.