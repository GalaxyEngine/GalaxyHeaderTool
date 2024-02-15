
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
	HeaderProperties headerProperties;

	std::string line;
	size_t lineNumber = 0;
	size_t bracketCount = 0;
	size_t currentLineNumber = 0;
	bool inClassScope = false;
	std::regex classRegex(R"(\bCLASS\(\)\s*)");
	std::regex generatedBodyRegex(R"(\bGENERATED_BODY\(\)\s*)");
	std::string classContent;
	while (std::getline(file, line))
	{
		++lineNumber;

		// Check if we are entering a class scope
		if (std::regex_search(line, classRegex))
		{
			inClassScope = true;
		}

		if (inClassScope)
		{
			classContent += line;
			classContent += '\n';
		}

		if (inClassScope && line.find('{') != std::string::npos)
		{
			bracketCount++;
		}
		if (inClassScope && line.find('}') != std::string::npos)
		{
			bracketCount--;

			if (bracketCount == 0)
			{
				ClassProperties classProperties;

				std::cout << classContent << std::endl;

				classProperties.lineNumber = currentLineNumber;

				ParseClassHeader(classContent, classProperties);

				if (classProperties.className.empty())
				{
					throw std::runtime_error("Class name not found");
				}

				ParseClassProperties(classContent, classProperties);

				ParseClassMethods(classContent, classProperties);

				headerProperties.classProperties.push_back(classProperties);

				inClassScope = false;
				classContent = "";
			}
		}

		// Look for GENERATED_BODY within the class scope
		if (inClassScope && std::regex_search(line, generatedBodyRegex))
		{
			currentLineNumber = lineNumber;
			std::cout << "GENERATED_BODY() found in class at line: " << lineNumber << std::endl;
		}
	}

	CreateGeneratedFile(path, headerProperties);

	CreateGenFile(path, headerProperties);
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
		R"(PROPERTY\(([^)]*)\)(?:\;|)\s*(?:class\s+|struct\s+)?((?:\w+::)*\w+(?:\s*<[^;<>]*(?:<(?:[^;<>]*)>)*[^;<>]*>)?\s*\*?)\s+(\w+)\s*(?:=\s*[^;]*)?;)"
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
		Property p;
		std::string arguments = match[1].str();

		std::istringstream iss(arguments);
		std::string token;

		while (std::getline(iss, token, ',')) {
			// Trim whitespace if necessary
			token.erase(0, token.find_first_not_of(' ')); // leading spaces
			token.erase(token.find_last_not_of(' ') + 1); // trailing spaces
			p.arguments.push_back(token);
		}

		p.type = match[2].str(); // Type of the variable, including namespace and template if any
		p.name = match[3].str(); // Name of the variable
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

void HeaderTool::ParseEnum(const std::string& enumContent, EnumProperties& properties)
{

}

void HeaderTool::CreateGeneratedFile(const std::filesystem::path& path, const HeaderProperties& properties)
{
	const std::filesystem::path fileName = m_generatedFolder / (path.filename().stem().string() + ".generated.h");
	std::ofstream outputFile(fileName);

	std::string fileContent;
	fileContent += "#pragma once\n";
	std::string endFile = "#undef END_FILE\n#define END_FILE()\\\n";


	auto path_define = path.generic_string();
	std::replace(path_define.begin(), path_define.end(), '/', '_');
	std::transform(path_define.begin(), path_define.end(), path_define.begin(), [](unsigned char c) {
		if (std::isalnum(c))
			return std::toupper(c);
		return std::toupper('_');
		});

	for (const auto& classProperties : properties.classProperties)
	{
		bool hasParent = classProperties.baseClassName != classProperties.className;
		std::string generatedBody;
		if (hasParent) {
			generatedBody = R"(#define %s_%d_GENERATED_BODY\
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
)";
		}
		else
		{
			generatedBody = R"(#define %s_%d_GENERATED_BODY\
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
)";
		}
		const char* className = classProperties.className.c_str();
		std::string generatedContent = string_format(generatedBody,
			path_define.c_str(), classProperties.lineNumber,
			className, className, className,
			classProperties.baseClassName.c_str(),
			className);

		endFile += string_format("\\\n\tEXPORT_FUNC void* Internal_Create_%s() {return new %s();}\\\n"
			, className, className);

		for (const auto& property : classProperties.properties)
		{
			const char* propertyName = property.name.c_str();
			const std::string content = R"(	EXPORT_FUNC void* Internal_Get_%s_%s(%s* object) {return &object->%s;}\
	EXPORT_FUNC void Internal_Set_%s_%s(%s* object, void* value){ object->%s = *reinterpret_cast<decltype(object->%s)*>(value);}\
)";
			endFile += string_format(content, className, propertyName, className, propertyName,
				className, propertyName, className, propertyName, propertyName);
		}

		if (!classProperties.methods.empty()) {

			for (const auto& method : classProperties.methods)
			{
				const char* methodName = method.c_str();
				const std::string content = R"(	EXPORT_FUNC void Internal_Call_%s_%s(%s* object) { object->%s();}\
)";
				endFile += string_format(content, className, methodName, className, methodName);
			}
		}
		generatedContent += "\n";
		fileContent += generatedContent;
	}
	fileContent += endFile;

	fileContent += "\n#undef CURRENT_FILE_ID\n#define CURRENT_FILE_ID ";

	fileContent += path_define;

	outputFile << fileContent;

	outputFile.close();

	std::cout << "Generated file " << fileName << std::endl;
}

void HeaderTool::CreateGenFile(const std::filesystem::path& path, const HeaderProperties& headerProperties) const
{
	using namespace CppSer;
	const std::filesystem::path fileName = m_generatedFolder / (path.filename().stem().string() + ".gen");
	Serializer serializer(fileName);
	for (const auto& classProperties : headerProperties.classProperties)
	{
		serializer << Pair::BeginMap << "Class";
		serializer << Pair::Key << "Class Name" << Pair::Value << classProperties.className;
		serializer << Pair::Key << "Property Size" << Pair::Value << classProperties.properties.size();
		for (const auto& property : classProperties.properties)
		{
			serializer << Pair::BeginMap << "Property";
			serializer << Pair::Key << "Argument Size" << Pair::Value << property.arguments.size();
			for (size_t i = 0; i < property.arguments.size(); i++)
			{
				serializer << Pair::Key << "Argument " + std::to_string(i) << Pair::Value << property.arguments[i];
			}
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
}
