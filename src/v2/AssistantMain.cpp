#include "AssistantOperations.h"

#include <cymcp/mcp.hpp>

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace {

constexpr std::string_view help =
    "Usage: cydawctl [--port=PORT] list | describe NAME | call NAME [JSON|-|@FILE]\n"
    "       cydawctl --help | --version\n"
    "Without --port, list and describe are local and list_factory_effects can be called offline.\n"
    "To control a live session, enable local MCP and choose its port, or start with --mcp-port=PORT.\n"
    "Set CYDAW_MCP_TOKEN only for a profile that requires a token.\n"
    "Use - for JSON on stdin or @FILE for a UTF-8 JSON file. Results go to stdout.\n";

constexpr std::size_t maxArguments = 64 * 1024;

std::string readArguments(std::istream& stream)
{
    std::string input(maxArguments + 1, '\0');
    stream.read(input.data(), static_cast<std::streamsize>(input.size()));
    input.resize(static_cast<std::size_t>(stream.gcount()));
    if (stream.bad() || input.size() > maxArguments)
        throw cymcp::Error("InvalidArguments", "Cannot read JSON arguments larger than 64 KiB");
    return input;
}

cymcp::Json arguments(const std::wstring& arg)
{
    if (arg == L"-")
        return cymcp::parse_json(readArguments(std::cin), maxArguments);
    if (arg.starts_with(L"@")) {
        std::ifstream file(std::filesystem::path(arg.substr(1)), std::ios::binary);
        if (!file)
            throw cymcp::Error("FileError", "Cannot open the JSON arguments file");
        return cymcp::parse_json(readArguments(file), maxArguments);
    }
    return cymcp::parse_json(cymcp::path_utf8(arg), maxArguments);
}

int portNumber(const std::wstring& arg)
{
    const auto number = cymcp::path_utf8(arg.substr(7));
    int port = 0;
    const auto last = number.data() + number.size();
    const auto parsed = std::from_chars(number.data(), last, port);
    if (parsed.ec != std::errc{} || parsed.ptr != last || port < 1 || port > 65535)
        throw cymcp::Error("InvalidArguments", "--port must be an integer between 1 and 65535");
    return port;
}

cymcp::Json call(cymcp::McpClient& client, const std::string& tool, const cymcp::Json& input,
                 const cymcp::Cancellation& cancellation)
{
    const auto reply = client.call_tool(tool, input, cancellation);
    if (reply.value("isError", false)) {
        const auto details = cymcp::parse_json(reply.at("content").at(0).at("text").get<std::string>());
        throw cymcp::Error(details.value("code", "ToolError"), details.value("message", "CyDaw rejected the operation"));
    }
    return reply.at("structuredContent");
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    const auto* secret = std::getenv("CYDAW_MCP_TOKEN");
    const std::string token = secret ? secret : "";
    try {
        int port = 0;
        std::vector<std::wstring> params;
        for (int i = 1; i < argc; ++i) {
            const std::wstring arg(argv[i]);
            if (arg.starts_with(L"--port=")) {
                if (port != 0)
                    throw cymcp::Error("InvalidArguments", "Specify only one --port");
                port = portNumber(arg);
            } else {
                params.push_back(arg);
            }
        }
        if (params.empty() || (params.size() == 1 && (params[0] == L"--help" || params[0] == L"help"))) {
            std::cout << help;
            return 0;
        }
        if (params.size() == 1 && params[0] == L"--version") {
            std::cout << cymcp::Json({{"sdkVersion", cymcp::version}, {"sdkRevision", CYDAW_MCP_REVISION}}).dump() << '\n';
            return 0;
        }
        const auto& command = params[0];
        if (!((command == L"list" && params.size() == 1) || (command == L"describe" && params.size() == 2)
              || (command == L"call" && (params.size() == 2 || params.size() == 3))))
            throw cymcp::Error("InvalidArguments", "Use cydawctl --help for the command syntax");
        const auto name = params.size() > 1 ? cymcp::path_utf8(params[1]) : std::string{};
        const auto input = params.size() == 3 ? arguments(params[2]) : cymcp::Json::object();
        cymcp::Json result;
        if (port == 0) {
            const auto registry = cydaw::v2::makeAssistantRegistry();
            if (command == L"list")
                result = registry->describe();
            else if (command == L"describe")
                result = registry->describe(name);
            else if (name == "list_factory_effects")
                result = registry->invoke(name, input);
            else
                throw cymcp::Error("InstanceRequired", "Use --port=PORT to call live operations");
        } else {
            cymcp::McpClient client("http://127.0.0.1:" + std::to_string(port) + "/mcp", token);
            cymcp::Cancellation deadline;
            deadline.deadline = cymcp::Clock::now() + std::chrono::seconds(35);
            client.connect(deadline);
            if (command == L"list")
                result = client.list_tools(deadline);
            else if (command == L"describe")
                result = client.get_tool(name, deadline);
            else
                result = call(client, name, input, deadline);
        }
        std::cout << result.dump() << '\n';
        return 0;
    } catch (const cymcp::Error& error) {
        std::cerr << cymcp::Json({{"error", {{"code", error.code},
            {"message", cymcp::redact_error_message(error.what(), {token})}}}}).dump() << '\n';
        return error.code == "InvalidArguments" ? 2 : 1;
    } catch (const std::exception& error) {
        std::cerr << cymcp::Json({{"error", {{"code", "UnexpectedError"},
            {"message", cymcp::redact_error_message(error.what(), {token})}}}}).dump() << '\n';
        return 1;
    }
}
