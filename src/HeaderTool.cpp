
#include "HeaderTool.h"
#include <regex>
#include <fstream>
#include <iostream>
#include <string>

#include <memory>
#include <stdexcept>

#include <cpp_serializer/CppSerializer.h>

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

	std::regex beginClassRegex(R"(\bCLASS\(\)\s*)");
	std::regex endClassRegex(R"(\bEND_CLASS\(\)\s*)");

	// Match iterators.
	std::smatch beginMatch;
	std::smatch endMatch;

	// Search for the beginning and end of the class.
	bool foundBegin = std::regex_search(content, beginMatch, beginClassRegex);
	bool foundEnd = std::regex_search(content, endMatch, endClassRegex);

	if (foundBegin && foundEnd) {
		// Calculate the start and end positions for the substring.
		auto contentStart = beginMatch[0].second - 3;
		auto contentEnd = endMatch[0].first;

		// Extract the content.
		content = std::string(contentStart, contentEnd);
	}
	else {
		return;
	}

	ClassProperties classProperties;

	ParseClassHeader(content, classProperties);

	if (classProperties.className.empty())
	{
		throw std::runtime_error("Class name not found");
	}

	ParseClassProperties(content, classProperties);

	ParseClassMethods(content, classProperties);

	CreateGeneratedFile(path, classProperties);

	CreateGenFile(path, classProperties);
}

void HeaderTool::ParseClassHeader(const std::string& classContent, ClassProperties& classProperties)
{
	// Regular expression to match class names with optional inheritance and an opening brace
	std::regex classRegex(R"(\bclass\s+(\w+)\s*(?:\s*:\s*(?:public)?\s+((?:\w+::)*\w+(?:<\w*>|)?))?\s*\{)");
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
	// It will check also if commented
	std::regex propertyRegex(
		R"(PROPERTY\(\)(?:\;|)\s*(?:class\s+|struct\s+)?((?:\w+::)*\w+(?:\s*<[^;<>]*(?:<(?:[^;<>]*)>)*[^;<>]*>)?\s*\*?)\s+(\w+)\s*(?:=\s*[^;]*)?;)"
	);
	std::smatch match;

	std::regex multiLineCommentsRegex(R"(/\*(?:[^*]|[\r\n]|(\*+([^*/]|[\r\n])))*\*+/)");
	std::sregex_iterator commentsBegin = std::sregex_iterator(classContent.begin(), classContent.end(), multiLineCommentsRegex);
	std::sregex_iterator commentsEnd = std::sregex_iterator();


	auto begin = std::sregex_iterator(classContent.begin(), classContent.end(), propertyRegex);
	auto end = std::sregex_iterator();

	std::vector<std::pair<size_t, size_t>> commentRanges;

	for (std::sregex_iterator i = commentsBegin; i != commentsEnd; ++i) {
		std::smatch matchComment = *i;
		commentRanges.emplace_back(matchComment.position(), matchComment.position() + matchComment.length());
	}

	for (std::sregex_iterator i = begin; i != end; ++i) {
		match = *i;

		size_t matchStartPos = match.position();
		bool isInComment = std::any_of(commentRanges.begin(), commentRanges.end(), [matchStartPos](const std::pair<size_t, size_t>& range) {
			return matchStartPos >= range.first && matchStartPos < range.second;
			});

		if (isInComment)
			continue; // Skip this PROPERTY() match as it's inside a multi-line comment

		// Check if '//' precedes the match in the same line
		auto line_start = classContent.rfind('\n', match.position()) + 1;
		auto comment_pos = classContent.find("//", line_start);
		if (!(comment_pos == std::string::npos || comment_pos > match.position()))
			continue;

		std::string typeName = match[1].str(); // Type of the variable, including namespace and template if any
		std::string varName = match[2].str(); // Name of the variable
		Property p = { typeName, varName };
		classProperties.properties.push_back(p);
	}
}

void HeaderTool::ParseClassMethods(const std::string& classContent, ClassProperties& classProperties)
{
	// Regular expression to match PROPERTY(); followed by an optional class or struct keyword,
// then a variable declaration that may include pointers, references, namespace prefixes, and template types
// It also allows for optional default values after an equals sign.
// It will check also if commented
	std::regex propertyRegex(
		R"(FUNCTION\(\)(?:\;|)\s*void\s*(\w*)\(\s*\))"
	);
	std::smatch match;

	std::regex multiLineCommentsRegex(R"(/\*(?:[^*]|[\r\n]|(\*+([^*/]|[\r\n])))*\*+/)");
	std::sregex_iterator commentsBegin = std::sregex_iterator(classContent.begin(), classContent.end(), multiLineCommentsRegex);
	std::sregex_iterator commentsEnd = std::sregex_iterator();


	auto begin = std::sregex_iterator(classContent.begin(), classContent.end(), propertyRegex);
	auto end = std::sregex_iterator();

	std::vector<std::pair<size_t, size_t>> commentRanges;

	for (std::sregex_iterator i = commentsBegin; i != commentsEnd; ++i) {
		std::smatch matchComment = *i;
		commentRanges.emplace_back(matchComment.position(), matchComment.position() + matchComment.length());
	}

	for (std::sregex_iterator i = begin; i != end; ++i) {
		match = *i;

		size_t matchStartPos = match.position();
		bool isInComment = std::any_of(commentRanges.begin(), commentRanges.end(), [matchStartPos](const std::pair<size_t, size_t>& range) {
			return matchStartPos >= range.first && matchStartPos < range.second;
			});

		if (isInComment)
			continue; // Skip this PROPERTY() match as it's inside a multi-line comment

		// Check if '//' precedes the match in the same line
		auto line_start = classContent.rfind('\n', match.position()) + 1;
		auto comment_pos = classContent.find("//", line_start);
		if (!(comment_pos == std::string::npos || comment_pos > match.position()))
			continue;

		std::string methodName = match[1].str(); // Type of the variable, including namespace and template if any
		classProperties.methods.push_back(methodName);
	}
}

void HeaderTool::CreateGeneratedFile(const std::filesystem::path& path, const ClassProperties& classProperties)
{
	const std::filesystem::path fileName = m_generatedFolder / (path.filename().stem().string() + ".generated.h");
	std::ofstream outputFile(fileName);

	bool hasParent = classProperties.baseClassName != classProperties.className;
	std::string topContent;
	if (hasParent) {
		topContent = R"(#pragma once
#undef GENERATED_BODY
#define GENERATED_BODY()\
	public:\
		virtual void* Clone() {\
			return new %s(*this);\
		}\
		\
		virtual const char* Internal_GetClassName() const {return "%s";}\
		virtual std::set<const char*> Internal_GetClassNames() const\
		{\
			std::set<const char*> list = Super::Internal_GetClassNames(); \
			list.insert(%s::Internal_GetClassName()); \
			return list;\
		}\
	private:\
		typedef %s Super;
#undef END_CLASS
#define END_CLASS()\
	EXPORT_FUNC inline void* Internal_Create_%s() {return new %s();}\
)";
	}
	else
	{
		topContent = R"(#pragma once
#undef GENERATED_BODY
#define GENERATED_BODY()\
	public:\
		virtual void* Clone() {\
			return new %s(*this);\
		}\
		\
		virtual const char* Internal_GetClassName() const {return "%s";}\
		virtual std::set<const char*> Internal_GetClassNames() const\
		{\
			std::set<const char*> list; \
			list.insert(%s::Internal_GetClassName());\
			return list;\
		}\
	private:\
		typedef %s Super;
#undef END_CLASS
#define END_CLASS()\
	EXPORT_FUNC void* Internal_Create_%s() {return new %s();}\
)";
	}
	const char* className = classProperties.className.c_str();
	std::string generatedContent = string_format(topContent,
		className, className, className,
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

	if (!classProperties.methods.empty()) {
		generatedContent += "\t\\\n";

		for (const auto& method : classProperties.methods)
		{
			const char* methodName = method.c_str();
			const std::string content = R"(	EXPORT_FUNC inline void Internal_Call_%s_%s(%s* object) { object->%s();}\
)";
			generatedContent += string_format(content, className, methodName, className, methodName);
		}
	}
	generatedContent += "\n";

	outputFile << generatedContent;

	outputFile.close();

	std::cout << "Generated file " << fileName << std::endl;
}

void HeaderTool::CreateGenFile(const std::filesystem::path& path, const ClassProperties& classProperties) const
{
	using namespace CppSer;
	const std::filesystem::path fileName = m_generatedFolder / (path.filename().stem().string() + ".gen");
	Serializer serializer(fileName);

	serializer << Pair::BeginMap << "Class";
	serializer << Pair::Key << "Class Name" << Pair::Value << classProperties.className;
	serializer << Pair::Key << "Property Size" << Pair::Value << classProperties.properties.size();
	for (const auto& property : classProperties.properties)
	{
		serializer << Pair::BeginMap << "Property";
		serializer << Pair::Key << "Name" << Pair::Value << property.name;
		serializer << Pair::Key << "Type" << Pair::Value << property.type;
		serializer << Pair::EndMap << "Property";
	}
	serializer << Pair::Key << "Method Size" << Pair::Value << classProperties.methods.size();
	for (const auto& method : classProperties.methods)
	{
		serializer << Pair::BeginMap << "Method";
		serializer << Pair::Key << "Name" << Pair::Value << method;
		serializer << Pair::EndMap << "Method";
	}
	serializer << Pair::EndMap << "Class";
}
