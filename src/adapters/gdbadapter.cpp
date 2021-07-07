#include "gdbadapter.h"
#include <memory>
#include <cstring>
#include <unistd.h>
#include <algorithm>
#include <string>
#include <chrono>
#include <thread>
#include <pugixml/pugixml.hpp>
#include <spawn.h>
#include "binaryninjaapi.h"
#include "lowlevelilinstruction.h"

using namespace BinaryNinja;

GdbAdapter::GdbAdapter()
{

}

GdbAdapter::~GdbAdapter()
{

}

std::string GdbAdapter::ExecuteShellCommand(const std::string& command)
{
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe)
        return {};

    std::string result{};
    std::array<char, 128> buffer{};
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
        result += buffer.data();

    if (result.empty())
        return {};

    return result;
}

bool GdbAdapter::Execute(const std::string& path)
{
    auto gdb_server_path = this->ExecuteShellCommand("which gdbserver");
    if ( gdb_server_path.empty() )
        return false;
    gdb_server_path.erase(std::remove(gdb_server_path.begin(), gdb_server_path.end(), '\n'), gdb_server_path.end());

    for ( int index = 31337; index < 31337 + 256; index++ )
    {
        this->m_socket = socket(AF_INET, SOCK_STREAM, 0);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons(index);

        if (bind(this->m_socket, (const sockaddr*) &addr, sizeof(addr)) >= 0)
        {
            this->m_port = index;
            close(this->m_socket);
            break;
        }
    }

    if ( !this->m_port )
        return false;

    std::array<char, 256> buffer{};
    std::sprintf(buffer.data(), "localhost:%d", this->m_port);
    char* arg[] = {"--once", "--no-startup-with-shell", buffer.data(), (char*) path.c_str(), NULL};

    pid_t pid = fork();
    switch (pid)
    {
    case -1:
        perror("fork");
        return false;
    case 0:
    {
        // This is done in the Python implementation, but I am not sure what it is intended for
        // setpgrp();

        // This will detach the gdbserver from the current terminal, so that we can continue interacting with it.
        // Otherwise, gdbserver will set itself to the foreground process and the cli will become background.
        // TODO: we should redirect the stdin/stdout to a different FILE so that we can see the debuggee's output
        // and send input to it
        FILE *newOut = freopen("/dev/null", "w", stdout);
        if (!newOut)
        {
            perror("freopen");
            return false;
        }

        FILE *newIn = freopen("/dev/null", "r", stdin);
        if (!newIn)
        {
            perror("freopen");
            return false;
        }

        FILE *newErr = freopen("/dev/null", "w", stderr);
        if (!newErr)
        {
            perror("freopen");
            return false;
        }

        if (execv(gdb_server_path.c_str(), arg) == -1)
        {
            perror("execv");
            return false;
        }
    }
    default:
        break;
    }

    return this->Connect("127.0.0.1", this->m_port);
}

bool GdbAdapter::ExecuteWithArgs(const std::string& path, const std::vector<std::string>& args)
{
    return false;
}

bool GdbAdapter::Attach(std::uint32_t pid)
{
    return true;
}

bool GdbAdapter::LoadRegisterInfo()
{
    const auto xml = this->m_rspConnector.GetXml("target.xml");

    pugi::xml_document doc{};
    const auto parse_result = doc.load_string(xml.c_str());
    if (!parse_result)
        return false;

    std::string architecture{};
    std::string os_abi{};
    for (auto node = doc.first_child().child("architecture"); node; node = node.next_sibling())
    {
        using namespace std::literals::string_literals;

        if ( node.name() == "architecture"s )
            architecture = node.child_value();
        if ( node.name() == "osabi"s )
            os_abi = node.child_value();

        if ( node.name() == "feature"s )
        {
            for (auto reg_child = node.child("reg"); reg_child; reg_child = reg_child.next_sibling())
            {
                std::string register_name{};
                RegisterInfo register_info{};

                for (auto reg_attribute = reg_child.attribute("name"); reg_attribute; reg_attribute = reg_attribute.next_attribute())
                {
                    if (reg_attribute.name() == "name"s )
                        register_name = reg_attribute.value();
                    else if (reg_attribute.name() == "bitsize"s )
                        register_info.m_bitSize = reg_attribute.as_uint();
                    else if (reg_attribute.name() == "regnum"s)
                        register_info.m_regNum = reg_attribute.as_uint();
                }

                this->m_registerInfo[register_name] = register_info;
            }
        }
    }

    std::unordered_map<std::uint32_t, std::string> id_name{};
    std::unordered_map<std::uint32_t, std::uint32_t> id_width{};

    for ( auto [key, value] : this->m_registerInfo ) {
        id_name[value.m_regNum] = key;
        id_width[value.m_regNum] = value.m_bitSize;
    }

    std::size_t max_id{};
    for ( auto [key, value] : this->m_registerInfo )
        max_id += value.m_regNum;

    std::size_t offset{};
    for ( std::size_t index{}; index < max_id; index++ ) {
        if ( !id_width[index] )
            break;

        const auto name = id_name[index];
        this->m_registerInfo[name].m_offset = offset;
        offset += id_width[index];
    }

    return true;
}

bool GdbAdapter::Connect(const std::string& server, std::uint32_t port)
{
    bool connected = false;
    for ( std::uint8_t index{}; index < 4; index++ )
    {
        this->m_socket = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons(this->m_port);
        if (connect(this->m_socket, (const sockaddr*) &addr, sizeof(addr)) >= 0)
        {
            connected = true;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if ( !connected ) {
        printf("failed to connect!\n");
        return false;
    }

    this->m_rspConnector = RspConnector(this->m_socket);
    printf("FINAL RESPONSE -> %s\n", this->m_rspConnector.TransmitAndReceive(RspData("Hg0")).AsString().c_str() );
    this->m_rspConnector.NegotiateCapabilities(
            { "swbreak+", "hwbreak+", "qRelocInsn+", "fork-events+", "vfork-events+", "exec-events+",
                         "vContSupported+", "QThreadEvents+", "no-resumed+", "xmlRegisters=i386" } );
    if ( !this->LoadRegisterInfo() )
        return false;

    auto reply = this->m_rspConnector.TransmitAndReceive(RspData("?"));
    printf("RESPONSE -> %s\n", reply.AsString().c_str() );
    auto map = RspConnector::PacketToUnorderedMap(reply);
    for ( const auto& [key, val] : map ) {
        printf("[%s] = 0x%llx\n", key.c_str(), val );
    }

    this->m_lastActiveThreadId = map["thread"];

    return true;
}

void GdbAdapter::Detach()
{

}

void GdbAdapter::Quit()
{

}

std::vector<DebugThread> GdbAdapter::GetThreadList()
{
    int internal_thread_index{};
    std::vector<DebugThread> threads{};

    auto reply = this->m_rspConnector.TransmitAndReceive(RspData("qfThreadInfo"));
    while(reply.m_data[0] != 'l') {
        printf("%s\n", reply.AsString().c_str());
        if (reply.m_data[0] != 'm')
            throw std::runtime_error("thread list failed?");

        const auto shortened_string =
                reply.AsString().substr(1);
        const auto tids = RspConnector::Split(shortened_string, ",");
        for ( const auto& tid : tids )
            threads.emplace_back(std::stoi(tid, nullptr, 16), internal_thread_index++);

        reply = this->m_rspConnector.TransmitAndReceive(RspData("qsThreadInfo"));
    }

    return threads;
}

DebugThread GdbAdapter::GetActiveThread() const
{
    return DebugThread();
}

std::uint32_t GdbAdapter::GetActiveThreadId() const
{
    return 0;
}

bool GdbAdapter::SetActiveThread(const DebugThread& thread)
{
    return false;
}

bool GdbAdapter::SetActiveThreadId(std::uint32_t tid)
{
    return false;
}

DebugBreakpoint GdbAdapter::AddBreakpoint(const std::uintptr_t address, unsigned long breakpoint_type)
{
    if ( std::find(this->m_debugBreakpoints.begin(), this->m_debugBreakpoints.end(),
                   DebugBreakpoint(address)) != this->m_debugBreakpoints.end())
        return {};

    /* TODO: replace %d with the actual breakpoint size as it differs per architecture */
    if (this->m_rspConnector.TransmitAndReceive(RspData("Z0,%llx,%d", address, 1)).AsString() != "OK" )
        throw std::runtime_error("rsp reply failure on breakpoint");

    const auto new_breakpoint = DebugBreakpoint(address, this->m_internalBreakpointId++, true);
    this->m_debugBreakpoints.push_back(new_breakpoint);

    return new_breakpoint;
}

std::vector<DebugBreakpoint> GdbAdapter::AddBreakpoints(const std::vector<std::uintptr_t>& breakpoints)
{
    return std::vector<DebugBreakpoint>();
}

bool GdbAdapter::RemoveBreakpoint(const DebugBreakpoint& breakpoint)
{
    return false;
}

bool GdbAdapter::RemoveBreakpoints(const std::vector<DebugBreakpoint>& breakpoints)
{
    return false;
}

bool GdbAdapter::ClearAllBreakpoints()
{
    return false;
}

std::vector<DebugBreakpoint> GdbAdapter::GetBreakpointList() const
{
    return this->m_debugBreakpoints;
}

std::string GdbAdapter::GetRegisterNameByIndex(std::uint32_t index) const
{
    return std::string();
}

bool GdbAdapter::UpdateRegisterCache() {
    if ( this->m_registerInfo.empty() )
        return false;

    std::vector<register_pair> register_info_vec{};
    for ( const auto& [register_name, register_info] : this->m_registerInfo ) {
        register_info_vec.emplace_back(register_name, register_info);
    }

    std::sort(register_info_vec.begin(), register_info_vec.end(),
              [](const register_pair& lhs, const register_pair& rhs) {
                  return lhs.second.m_regNum < rhs.second.m_regNum;
              });

    char request{'g'};
    const auto register_info_reply = this->m_rspConnector.TransmitAndReceive(RspData(&request, sizeof(request)));
    auto register_info_reply_string = register_info_reply.AsString();
    if ( register_info_reply_string.empty() )
        return false;

    for ( const auto& [register_name, register_info] : register_info_vec ) {
        const auto number_of_chars = 2 * ( register_info.m_bitSize / 8 );
        const auto value_string = register_info_reply_string.substr(0, number_of_chars);
        if (number_of_chars <= 0x10) {
            const auto value = RspConnector::SwapEndianness(std::stoull(value_string, nullptr, 16));
            this->m_cachedRegisterInfo[register_name] = DebugRegister(register_name, value, register_info.m_bitSize);
            // #warning "ignoring registers with a larger size than 0x10"
            /* TODO: ^fix this^ */
        }
        register_info_reply_string.erase(0, number_of_chars);
    }

    return true;
}

DebugRegister GdbAdapter::ReadRegister(const std::string& reg)
{
    if ( this->m_registerInfo.find(reg) == this->m_registerInfo.end() )
        throw std::runtime_error("register does not exist in target");

    return this->m_cachedRegisterInfo[reg];
}

bool GdbAdapter::WriteRegister(const std::string& reg, std::uintptr_t value)
{
    if (!this->UpdateRegisterCache())
        throw std::runtime_error("failed to update register cache");

    char buf[64];
    std::sprintf(buf, "%016lX", RspConnector::SwapEndianness(value));
    const auto reply = this->m_rspConnector.TransmitAndReceive(RspData("P%x=%s", this->m_registerInfo[reg].m_regNum, buf));
    if (reply.m_data[0])
        return true;

    char query{'g'};
    const auto generic_query = this->m_rspConnector.TransmitAndReceive(RspData(&query, sizeof(query)));
    const auto register_offset = this->m_registerInfo[reg].m_offset;

    const auto first_half = generic_query.AsString().substr(0, 2 * (register_offset / 8));
    const auto second_half = generic_query.AsString().substr(2 * ((register_offset + this->m_registerInfo[reg].m_bitSize) / 8) );
    const auto payload = "G" + first_half + buf + second_half;

    if ( this->m_rspConnector.TransmitAndReceive(RspData(payload)).AsString() != "OK" )
        return false;

    return true;
}

bool GdbAdapter::WriteRegister(const DebugRegister& reg, std::uintptr_t value)
{
    return this->WriteRegister(reg.m_name, value);
}

std::vector<std::string> GdbAdapter::GetRegisterList() const
{
    std::vector<std::string> registers{};

    for ( const auto& [register_name, register_info] : this->m_registerInfo )
        registers.push_back(register_name);

    return registers;
}

bool GdbAdapter::ReadMemory(std::uintptr_t address, void* out, std::size_t size)
{
    if (!this->UpdateRegisterCache())
        throw std::runtime_error("failed to update register cache");

    auto reply = this->m_rspConnector.TransmitAndReceive(RspData("m%llx,%x", address, size));
    if (reply.m_data[0] == 'E')
        return false;

    const auto source = std::make_unique<std::uint8_t[]>(size + 1);
    const auto dest = std::make_unique<std::uint8_t[]>(size + 1);
    std::memset(source.get(), '\0', size + 1);
    std::memset(dest.get(), '\0', size + 1);
    std::memcpy(source.get(), reply.m_data, size);

    [](const std::uint8_t* src, std::uint8_t* dst) {
        const auto char_to_int = [](std::uint8_t input) -> int {
            if(input >= '0' && input <= '9')
                return input - '0';
            if(input >= 'A' && input <= 'F')
                return input - 'A' + 10;
            if(input >= 'a' && input <= 'f')
                return input - 'a' + 10;
            throw std::invalid_argument("Invalid input string");
        };

        while(*src && src[1]) {
            *(dst++) = char_to_int(*src) * 16 + char_to_int(src[1]);
            src += 2;
        }
    }(source.get(), dest.get());

    std::memcpy(out, dest.get(), size);

    return true;
}

bool GdbAdapter::WriteMemory(std::uintptr_t address, void* out, std::size_t size)
{
    if (!this->UpdateRegisterCache())
        throw std::runtime_error("failed to update register cache");

    const auto dest = std::make_unique<char[]>(size + 1);
    std::memset(dest.get(), '\0', size + 1);

    for ( std::size_t index{}; index < size; index++ )
        std::sprintf(dest.get(), "%s%02x", dest.get(), ((std::uint8_t*)out)[index]);

    auto reply = this->m_rspConnector.TransmitAndReceive(RspData("M%llx,%x:%s", address, size, dest.get()));
    if (reply.AsString() != "OK")
        return false;

    return true;
}

std::vector<DebugModule> GdbAdapter::GetModuleList()
{
    /* TODO: finish this */
    const auto path = "/proc/" + std::to_string(this->m_lastActiveThreadId) + "/maps";
    /*const auto set_filesystem = */this->m_rspConnector.TransmitAndReceive(RspData("vFile:setfs:0", "host_io"));

    std::string path_hex_string{};
    for ( auto c : path ) {
        char buf[0x4]{'\0'};
        std::sprintf(buf, "%02X", c);
        path_hex_string.append(buf);
    }

    const auto test =
            this->m_rspConnector.TransmitAndReceive(
                    RspData("vFile:open:%s,%X,%X", path_hex_string.c_str(), 0, 0), "host_io");

    return {};
}

std::string GdbAdapter::GetTargetArchitecture()
{
    const auto xml = this->m_rspConnector.GetXml("target.xml");

    pugi::xml_document doc{};
    const auto parse_result = doc.load_string(xml.c_str());
    if (!parse_result)
        throw std::runtime_error("failed to parse target.xml");

    std::string architecture{};
    for (auto node = doc.first_child().child("architecture"); node; node = node.next_sibling()) {
        using namespace std::literals::string_literals;
        if (node.name() == "architecture"s) {
            architecture = node.child_value();
            break;
        }
    }

    if (architecture.empty())
        throw std::runtime_error("failed to find architecture");

    architecture.erase(0, architecture.find(':') + 1);
    architecture.replace(architecture.find('-'), 1, "_");

    return architecture;
}

bool GdbAdapter::BreakInto()
{
    if (!this->UpdateRegisterCache())
        throw std::runtime_error("failed to update register cache");

    char var = '\x03';
    this->m_rspConnector.SendRaw(RspData(&var, sizeof(var)));
    return true;
}

bool GdbAdapter::GenericGo(const std::string& go_type) {
    if (!this->UpdateRegisterCache())
        throw std::runtime_error("failed to update register cache");

    const auto go_reply =
            this->m_rspConnector.TransmitAndReceive(
                    RspData(go_type), "mixed_output_ack_then_reply", true);

    if ( go_reply.m_data[0] == 'T' ) {
        auto map = RspConnector::PacketToUnorderedMap(go_reply);
        const auto tid = map["thread"];
        this->m_lastActiveThreadId = tid;
    } else if ( go_reply.m_data[0] == 'W' ) {
        /* exit status, substr */
    } else {
        printf("[generic go failed?]\n");
        printf("%s\n", go_reply.AsString().c_str());
        return false;
    }

    return true;
}

bool GdbAdapter::Go()
{
    return this->GenericGo("vCont;c:-1");
}

bool GdbAdapter::StepInto()
{
    return this->GenericGo("vCont;s");
}

bool GdbAdapter::StepOver()
{
    if (!this->UpdateRegisterCache())
        throw std::runtime_error("failed to update register cache");

    const auto instruction_offset = this->GetInstructionOffset();
    if (!instruction_offset)
        return false;

    const auto architecture = Architecture::GetByName(this->GetTargetArchitecture());
    if (!architecture)
        return false;

    const auto data = this->ReadMemoryTy<std::array<std::uint8_t, 8>>(instruction_offset);
    if (!data.has_value())
        return false;

    const auto data_value = data.value();
    std::size_t size{data_value.size()};
    std::vector<BinaryNinja::InstructionTextToken> instruction_tokens{};
    if (!architecture->GetInstructionText(data.value().data(), instruction_offset, size, instruction_tokens)) {
        printf("failed to disassemble\n");
        return false;
    }

    auto data_buffer = DataBuffer(data_value.data(), size);
    Ref<BinaryData> bd = new BinaryData(new FileMetadata(), data_buffer);
    Ref<BinaryView> bv;
    for (const auto& type : BinaryViewType::GetViewTypes()) {
        if (type->IsTypeValidForData(bd) && type->GetName() == "Raw") {
            bv = type->Create(bd);
            break;
        }
    }

    bv->UpdateAnalysisAndWait();

    Ref<Platform> plat = nullptr;
    auto arch_list = Platform::GetList();
    for ( const auto& arch : arch_list ) {
        constexpr auto os =
#ifdef WIN32
                "windows";
#else
                "linux";
#endif

        using namespace std::string_literals;
        if ( arch->GetName() == os + "-"s + this->GetTargetArchitecture() )
        {
            plat = arch;
            break;
        }
    }

    bv->AddFunctionForAnalysis(plat, 0);

    bool is_call{false };
    for (auto& func : bv->GetAnalysisFunctionList()) {
        if (is_call)
            break;

        Ref<LowLevelILFunction> llil = func->GetLowLevelIL();
        if (!llil)
            continue;

        for (const auto& llil_block : llil->GetBasicBlocks()) {
            if (is_call)
                break;

            for (std::size_t llil_index = llil_block->GetStart(); llil_index < llil_block->GetEnd(); llil_index++) {
                const auto current_llil_instruction = llil->GetInstruction(llil_index);
                const auto op = current_llil_instruction.operation;
                if ( op == LLIL_CALL ) {
                    is_call = true;
                    break;
                }
            }
        }
    }

    if ( !is_call ) {
        this->StepInto();
        return true;
    }

    /* TODO: test this on different architectures */
    std::size_t instruction_size = instruction_tokens[0].width + 1;

    const auto instruction_bp = this->AddBreakpoint(instruction_offset + instruction_size);
    this->Go();
    this->RemoveBreakpoint(instruction_bp);

    return true;
}

bool GdbAdapter::StepOut()
{
    /* TODO: when not in a cli interface we should leverage
     * TODO: already known binary view data of the control flow graph
     * TODO: to figure out where the function end point is located */
    throw std::runtime_error("step out not implemented in cli interface");
}

bool GdbAdapter::StepTo(std::uintptr_t address)
{
    if (!this->UpdateRegisterCache())
        throw std::runtime_error("failed to update register cache");

    const auto breakpoints = this->m_debugBreakpoints;

    this->RemoveBreakpoints(this->m_debugBreakpoints);

    const auto bp = this->AddBreakpoint(address);
    if ( !bp.m_address )
        return false;

    this->Go();

    this->RemoveBreakpoint(bp);

    for ( const auto& breakpoint : breakpoints )
        this->AddBreakpoint(breakpoint.m_address);

    return true;
}

void GdbAdapter::Invoke(const std::string& command)
{

}

std::uintptr_t GdbAdapter::GetInstructionOffset()
{
    if (!this->UpdateRegisterCache())
        throw std::runtime_error("failed to update register cache");

    return this->ReadRegister(this->GetTargetArchitecture() == "x86" ? "eip" : "rip").m_value;
}

unsigned long GdbAdapter::StopReason()
{
    return 0;
}

unsigned long GdbAdapter::ExecStatus()
{
    return 0;
}