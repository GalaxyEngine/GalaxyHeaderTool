
#include "HeaderTool.h"
#include <regex>
#include <fstream>
#include <iostream>
#include <string>
#include <format>

#include <memory>
#include <stdexcept>

#include "Parser.h"

template<typename ... Args>
std::string string_format(const std::string& format, Args ... args)
{
	int size_s = std::snprintf(nullptr, 0, format.c_str(), args ...) + 1; // Extra space for '\0'
	if (size_s <= 0) { throw std::runtime_error("Error during formatting."); }
	auto size = static_cast<size_t>(size_s);
	std::unique_ptr<char[]> buf(new char[size]);
	std::snprintf(buf.get(), size, format.c_str(), args ...);
	return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
}

void HeaderTool::ParseFiles(const std::filesystem::path& path)
{
	for (const auto& entry : std::filesystem::recursive_directory_iterator(path))
	{
		if (entry.path().string().find(".generated") != std::string::npos)
			continue;
		if (entry.is_regular_file() && entry.path().extension() == ".h" || entry.path().extension() == ".hpp")
		{
			ParseHeaderFile(entry.path());
		}
	}
}

void HeaderTool::ParseHeaderFile(const std::filesystem::path& path)
{
	const std::filesystem::path generatedPath = m_generatedFolder / (path.filename().stem().string() + ".generated.h");
#ifndef DEBUG
	if (std::filesystem::exists(generatedPath))
	{
		const auto sourceTime = std::filesystem::last_write_time(path);
		const auto generatedTime = std::filesystem::last_write_time(generatedPath);
		if (generatedTime >= sourceTime) {
			return; // The generated file is up-to-date
		}
	}
#endif

	std::ifstream file(path);
	if (!file.is_open())
	{
		std::cerr << "Failed to open file: " << path << std::endl;
		return;
	}

	std::string content((std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>());

	std::regex beginClassRegex(R"(CLASS\(\)(?:\;|)\s)");
	std::regex endClassRegex(R"(END_CLASS\(\)(?:\;|)(\s*))");

	// Match iterators.
	std::smatch beginMatch;
	std::smatch endMatch;

	// Search for the beginning and end of the class.
	bool foundBegin = std::regex_search(content, beginMatch, beginClassRegex);
	bool foundEnd = std::regex_search(content, endMatch, endClassRegex);

	if (foundBegin && foundEnd) {
		// Calculate the start and end positions for the substring.
		auto contentStart = beginMatch[0].second;
		auto contentEnd = endMatch[0].first;

		// Extract the content.
		content = std::string(contentStart, contentEnd);
	}
	else {
		return;
	}

	ClassProperties classProperties;

	ParseClassHeader(content, classProperties);

	ParseClassProperties(content, classProperties);

	CreateGeneratedFile(path, classProperties);

	CreateGenFile(path, classProperties);
}

void HeaderTool::ParseClassHeader(const std::string& classContent, ClassProperties& classProperties)
{
	// Regular expression to match class names with optional inheritance and an opening brace
	std::regex classRegex(R"(\bclass\s+(\w+)\s*:\s*public\s+((?:\w+::)*\w+)\s*\{)");
	std::smatch match;

	// Iterate over all matches in the file content
	auto begin = std::sregex_iterator(classContent.begin(), classContent.end(), classRegex);
	auto end = std::sregex_iterator();

	for (std::sregex_iterator i = begin; i != end; ++i) {
		match = *i;
		std::string className = match[1].str();
		std::string baseClassName = match.size() > 2 ? match[2].str() : "";

		classProperties.className = className;

		if (!baseClassName.empty()) {
			classProperties.baseClassName = baseClassName;
		}
		else
		{
			classProperties.baseClassName = classProperties.className;
		}
		break;
	}
}

void HeaderTool::ParseClassProperties(const std::string& classContent, ClassProperties& classProperties)
{
	// Regular expression to match PROPERTY(); followed by an optional class or struct keyword,
	// then a variable declaration that may include pointers, references, namespace prefixes, and template types
	// It also allows for optional default values after an equals sign.
	std::regex propertyRegex(R"(PROPERTY\(\)(?:\;|)\s*(?:class\s+|struct\s+)?((?:\w+::)*\w+(?:\s*<[^;<>]*(?:<(?:[^;<>]*)>)*[^;<>]*>)?\s*\*?)\s+(\w+)\s*(?:=\s*[^;]*)?;)");
	std::smatch match;

	auto begin = std::sregex_iterator(classContent.begin(), classContent.end(), propertyRegex);
	auto end = std::sregex_iterator();

	for (std::sregex_iterator i = begin; i != end; ++i) {
		match = *i;
		std::string typeName = match[1].str(); // Type of the variable, including namespace and template if any
		std::string varName = match[2].str(); // Name of the variable
		Property p = { typeName, varName };
		classProperties.properties.push_back(p);
	}
}

void HeaderTool::CreateGeneratedFile(const std::filesystem::path& path, const ClassProperties& classProperties)
{
	const std::filesystem::path fileName = m_generatedFolder / (path.filename().stem().string() + ".generated.h");
	std::ofstream outputFile(fileName);

	const std::string topContent = R"(#pragma once
#include <memory>
#undef GENERATED_BODY
#define GENERATED_BODY()\
	public:\
		virtual void* Clone() {\
			return new %s(*this);\
		}\
		\
		virtual const char* GetClassName() const {return "%s";}\
	private:\
		typedef %s Super;
#undef END_CLASS
#define END_CLASS()\
	EXPORT_FUNC void* Internal_Create_%s() {return new %s();}\
)";
	const char* className = classProperties.className.c_str();
	std::string generatedContent = string_format(topContent, 
		className, className, 
		classProperties.baseClassName.c_str(),
		className, className);

	for (const auto& property : classProperties.properties)
	{
		const char* propertyName = property.name.c_str();
		const std::string content = R"(	EXPORT_FUNC inline void* Internal_Get_%s_%s(%s* object) {return &object->%s;}\
	EXPORT_FUNC inline void Internal_Set_%s_%s(%s* object, void* value){ object->%s = *reinterpret_cast<decltype(object->%s)*>(value);}\
)";
		generatedContent += string_format(content, className, propertyName, className, propertyName,
			className, propertyName, className, propertyName, propertyName);
	}
	generatedContent += "\n";
	
	outputFile << generatedContent;

	outputFile.close();

	std::cout << "Generated file " << fileName << std::endl;
}

void HeaderTool::CreateGenFile(const std::filesystem::path& path, const ClassProperties& classProperties)
{
	const std::filesystem::path fileName = m_generatedFolder / (path.filename().stem().string() + ".gen");
	Serializer serializer(fileName);

	serializer << Pair::BEGIN_MAP << "Class";
	serializer << Pair::KEY << "Class Name" << Pair::VALUE << classProperties.className;
	serializer << Pair::KEY << "Property Size" << Pair::VALUE << classProperties.properties.size();
	for (const auto& property : classProperties.properties)
	{
		serializer << Pair::BEGIN_MAP << "Property";
		serializer << Pair::KEY << "Name" << Pair::VALUE << property.name;
		serializer << Pair::KEY << "Type" << Pair::VALUE << property.type;
		serializer << Pair::END_MAP << "Property";
	}
	serializer << Pair::END_MAP << "Class";
}
