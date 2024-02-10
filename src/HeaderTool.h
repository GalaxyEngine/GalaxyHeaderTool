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
	std::string baseClassName;
	std::string className;
	std::vector<std::string> methods;
	std::vector<Property> properties;
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

	void CreateGeneratedFile(const std::filesystem::path& path, const ClassProperties& properties);

	void CreateGenFile(const std::filesystem::path& path, const ClassProperties& properties) const;
private:
	std::filesystem::path m_generatedFolder;
};
