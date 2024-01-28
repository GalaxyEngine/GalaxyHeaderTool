
#include "HeaderTool.h"
#include <regex>
#include <fstream>
#include <iostream>
#include <string>
#include <format>

#include <memory>
#include <stdexcept>

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

	ClassProperties properties;

	ParseClassHeader(content, properties);

	ParseClassProperties(content, properties);

	CreateGeneratedFile(path, properties);
}

void HeaderTool::ParseClassHeader(const std::string& classContent, ClassProperties& properties)
{
	// Regular expression to match class names with optional inheritance and an opening brace
	std::regex classRegex(R"(\bclass\s+(\w+)(?:\s*:\s*public\s+(\w+))?\s*\{)");
	std::smatch match;

	// Iterate over all matches in the file content
	auto begin = std::sregex_iterator(classContent.begin(), classContent.end(), classRegex);
	auto end = std::sregex_iterator();

	for (std::sregex_iterator i = begin; i != end; ++i) {
		match = *i;
		std::string className = match[1].str();
		std::string baseClassName = match.size() > 2 ? match[2].str() : "";

		properties.className = className;

		if (!baseClassName.empty()) {
			properties.baseClassName = baseClassName;
		}
		else
		{
			properties.baseClassName = properties.className;
		}
		break;
	}
}

void HeaderTool::ParseClassProperties(const std::string& classContent, ClassProperties& properties)
{
	// Regular expression to match PROPERTY(); followed by an optional class or struct keyword,
	// then a variable declaration that may include pointers, references, namespace prefixes, and template types
	// It also allows for optional default values after an equals sign.
	std::regex propertyRegex(R"(PROPERTY\(\)\s*(?:class\s+|struct\s+)?((?:\w+::)*\w+(?:\s*<[^;<>]*(?:<(?:[^;<>]*)>)*[^;<>]*>)?)\s*[*&]?\s*(\w+)\s*(?:=\s*[^;]*)?;)");
	std::smatch match;

	auto begin = std::sregex_iterator(classContent.begin(), classContent.end(), propertyRegex);
	auto end = std::sregex_iterator();

	for (std::sregex_iterator i = begin; i != end; ++i) {
		match = *i;
		std::string typeName = match[1].str(); // Type of the variable, including namespace and template if any
		std::string varName = match[2].str(); // Name of the variable

		properties.properties.push_back(varName);
	}
}

void HeaderTool::CreateGeneratedFile(const std::filesystem::path& path, const ClassProperties& properties)
{
	const std::filesystem::path fileName = m_generatedFolder / (path.filename().stem().string() + ".generated.h");
	std::ofstream outputFile(fileName);

	const std::string topContent = R"(#pragma once
#include <memory>
#undef GENERATED_BODY
#define GENERATED_BODY()\
	public:\
		virtual void* Clone() override {\
			return new %s(*this);\
		}\
		\
		virtual const char* GetComponentName() const override {return "%s";}\
	private:\
		typedef %s Super;
#undef END_CLASS
#define END_CLASS()\
	EXPORT_FUNC void* Create_%s() {return new %s();}\
)";
	std::string generatedContent = string_format(topContent, 
		properties.className.c_str(), properties.className.c_str(), properties.className.c_str(), 
		properties.baseClassName.c_str(),
		properties.className.c_str(), properties.className.c_str());
	
	for (const auto& property : properties.properties)
	{
		const std::string content = R"(	EXPORT_FUNC inline void* Get_%s_%s(%s* object) {return &object->%s;}\
	EXPORT_FUNC inline void Set_%s_%s(%s* object, void* value){ object->%s = *reinterpret_cast<decltype(object->%s)*>(value);}\
)";
		generatedContent += string_format(content, properties.className.c_str(), property.c_str(), properties.className.c_str(), property.c_str(),
			properties.className.c_str(), property.c_str(), properties.className.c_str(), property.c_str(), property.c_str());
	}
	generatedContent += "\n";
	
	outputFile << generatedContent;

	outputFile.close();

	std::cout << "Generated file " << fileName << std::endl;
}