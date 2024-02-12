#pragma once
#include <filesystem>
#include <vector>

struct Property
{
	std::string type;
	std::string name;
};

struct ClassProperties
{
	size_t lineNumber;
	std::string baseClassName;
	std::string className;
	std::vector<std::string> methods;
	std::vector<Property> properties;
};

struct EnumProperties
{
	std::string name;
	std::vector<std::string> values;
};

struct HeaderProperties
{
	std::vector<ClassProperties> classProperties;
	std::vector<EnumProperties> enumProperties;
};

class HeaderTool
{
public:
	void ParseFiles(const std::filesystem::path& path);

	void SetGeneratedFolder(const std::filesystem::path& val) { m_generatedFolder = val; std::filesystem::create_directories(m_generatedFolder); }
private:
	void ParseHeaderFile(const std::filesystem::path& path);

	void ParseClassHeader(const std::string& classContent, ClassProperties& properties);

	void ParseClassProperties(const std::string& classContent, ClassProperties& properties);

	void ParseClassMethods(const std::string& classContent, ClassProperties& properties);

	void ParseEnum(const std::string& enumContent, EnumProperties& properties);

	void CreateGeneratedFile(const std::filesystem::path& path, const HeaderProperties& properties);

	void CreateGenFile(const std::filesystem::path& path, const HeaderProperties& properties) const;
private:
	std::filesystem::path m_generatedFolder;
};
