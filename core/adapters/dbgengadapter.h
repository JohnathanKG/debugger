#pragma once
#include "../debugadapter.h"
#include "../debugadaptertype.h"

#define NOMINMAX
#include <windows.h>
#include <dbgeng.h>
#include <chrono>

namespace BinaryNinjaDebugger
{
	struct ProcessCallbackInformation
	{
		bool m_created{false};
		bool m_exited{false};
		bool m_hasOneBreakpoint{false};
		DebugBreakpoint m_lastBreakpoint{};
		EXCEPTION_RECORD64 m_lastException{};
		std::uint64_t m_imageBase{};
		unsigned long m_exitCode{};
		unsigned long m_lastSessionStatus{DEBUG_SESSION_FAILURE};
	};

	#define CALLBACK_METHOD(return_type) return_type __declspec(nothrow) __stdcall
	class DbgEngOutputCallbacks : public IDebugOutputCallbacks
	{
	public:
		CALLBACK_METHOD(unsigned long) AddRef() override;
		CALLBACK_METHOD(unsigned long) Release() override;
		CALLBACK_METHOD(HRESULT) QueryInterface(const IID& interface_id, void** _interface) override;
		CALLBACK_METHOD(HRESULT) Output(unsigned long mask, const char* text);
	};

	class DbgEngEventCallbacks : public DebugBaseEventCallbacks
	{
	public:
		CALLBACK_METHOD(unsigned long) AddRef() override;
		CALLBACK_METHOD(unsigned long) Release() override;
		CALLBACK_METHOD(HRESULT) GetInterestMask(unsigned long* mask) override;
		CALLBACK_METHOD(HRESULT) Breakpoint(IDebugBreakpoint* breakpoint) override;
		CALLBACK_METHOD(HRESULT) Exception(EXCEPTION_RECORD64* exception, unsigned long first_chance) override;
		CALLBACK_METHOD(HRESULT) CreateThread(std::uint64_t handle, std::uint64_t data_offset, std::uint64_t start_offset) override;
		CALLBACK_METHOD(HRESULT) ExitThread(unsigned long exit_code) override;
		CALLBACK_METHOD(HRESULT) CreateProcess(
				std::uint64_t image_file_handle,
				std::uint64_t handle,
				std::uint64_t base_offset,
				unsigned long module_size,
				const char* module_name,
				const char* image_name,
				unsigned long check_sum,
				unsigned long time_date_stamp,
				std::uint64_t initial_thread_handle,
				std::uint64_t thread_data_offset,
				std::uint64_t start_offset
		) override;
		CALLBACK_METHOD(HRESULT) ExitProcess(unsigned long exit_code) override;
		CALLBACK_METHOD(HRESULT) LoadModule(
				std::uint64_t image_file_handle,
				std::uint64_t base_offset,
				unsigned long module_size,
				const char* module_name,
				const char* image_name,
				unsigned long check_sum,
				unsigned long time_date_stamp
		) override;
		CALLBACK_METHOD(HRESULT) UnloadModule(const char* image_base_name, std::uint64_t base_offset) override;
		CALLBACK_METHOD(HRESULT) SystemError(unsigned long error, unsigned long level) override;
		CALLBACK_METHOD(HRESULT) SessionStatus(unsigned long session_status) override;
		CALLBACK_METHOD(HRESULT) ChangeDebuggeeState(unsigned long flags, std::uint64_t argument) override;
		CALLBACK_METHOD(HRESULT) ChangeEngineState(unsigned long flags, std::uint64_t argument) override;
		CALLBACK_METHOD(HRESULT) ChangeSymbolState(unsigned long flags, std::uint64_t argument) override;
	};
	#undef CALLBACK_METHOD

	class DbgEngAdapter : public DebugAdapter
	{
		DbgEngEventCallbacks m_debugEventCallbacks{};
		DbgEngOutputCallbacks m_outputCallbacks{};
		IDebugClient5* m_debugClient{nullptr};
		IDebugControl5* m_debugControl{nullptr};
		IDebugDataSpaces* m_debugDataSpaces{nullptr};
		IDebugRegisters* m_debugRegisters{nullptr};
		IDebugSymbols* m_debugSymbols{nullptr};
		IDebugSystemObjects* m_debugSystemObjects{nullptr};
		bool m_debugActive{false};

		void Start();
		void Reset();
		bool Wait(std::chrono::milliseconds timeout = std::chrono::milliseconds::max());

		std::vector<DebugBreakpoint> m_debug_breakpoints{};

	public:
		inline static ProcessCallbackInformation ProcessCallbackInfo{};
		static constexpr unsigned long StepoutBreakpointID = 0x5be9c948;

		DbgEngAdapter();
		~DbgEngAdapter();

		[[nodiscard]] bool Execute(const std::string& path, const LaunchConfigurations& configs = {}) override;
		[[nodiscard]] bool ExecuteWithArgs(const std::string& path, const std::string &args,
										   const LaunchConfigurations& configs = {}) override;
		[[nodiscard]] bool Attach(std::uint32_t pid) override;
		[[nodiscard]] bool Connect(const std::string &server, std::uint32_t port) override;

		void Detach() override;
		void Quit() override;

		std::vector<DebugThread> GetThreadList() override;
		DebugThread GetActiveThread() const override;
		std::uint32_t GetActiveThreadId() const override;
		bool SetActiveThread(const DebugThread &thread) override;
		bool SetActiveThreadId(std::uint32_t tid) override;

		DebugBreakpoint AddBreakpoint(const std::uintptr_t address, unsigned long breakpoint_flags = 0) override;
		std::vector<DebugBreakpoint> AddBreakpoints(const std::vector<std::uintptr_t>& breakpoints) override;
		bool RemoveBreakpoint(const DebugBreakpoint &breakpoint) override;
		bool RemoveBreakpoints(const std::vector<DebugBreakpoint> &breakpoints) override;
		bool ClearAllBreakpoints() override;
		std::vector<DebugBreakpoint> GetBreakpointList() const override;

		std::string GetRegisterNameByIndex(std::uint32_t index) const override;
		std::unordered_map<std::string, DebugRegister> ReadAllRegisters() override;
		DebugRegister ReadRegister(const std::string &reg) override;
		bool WriteRegister(const std::string &reg, std::uintptr_t value) override;
		bool WriteRegister(const DebugRegister& reg, std::uintptr_t value) override;
		std::vector<std::string> GetRegisterList() const override;

		DataBuffer ReadMemory(std::uintptr_t address, std::size_t size) override;
		bool WriteMemory(std::uintptr_t address, const DataBuffer& buffer) override;

		//bool ReadMemory(std::uintptr_t address, void* out, std::size_t size) override;
		//bool WriteMemory(std::uintptr_t address, const void* out, std::size_t size) override;
		std::vector<DebugModule> GetModuleList() override;

		std::string GetTargetArchitecture() override;

		BNDebugStopReason StopReason() override;
		unsigned long ExecStatus() override;
		uint64_t ExitCode() override;

		bool BreakInto() override;
		BNDebugStopReason Go() override;
		BNDebugStopReason StepInto() override;
		BNDebugStopReason StepOver() override;
	//    bool StepTo(std::uintptr_t address) override;

		void Invoke(const std::string& command) override;
		std::uintptr_t GetInstructionOffset() override;

		bool SupportFeature(DebugAdapterCapacity feature) override;
	};

	class LocalDbgEngAdapterType: public DebugAdapterType
	{
	public:
		LocalDbgEngAdapterType();
		virtual DebugAdapter* Create(BinaryNinja::BinaryView* data);
		virtual bool IsValidForData(BinaryNinja::BinaryView* data);
		virtual bool CanExecute(BinaryNinja::BinaryView* data);
		virtual bool CanConnect(BinaryNinja::BinaryView* data);
	};


	void InitDbgEngAdapterType();
};