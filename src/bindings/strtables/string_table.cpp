#include "string_table.hpp"
#include "../../main.hpp"

namespace vmod::bindings::strtables
{
	vscript::class_desc<string_table> string_table::desc{"strtables::string_table"};

	bool string_table::bindings() noexcept
	{
		using namespace std::literals::string_view_literals;

		gsdk::IScriptVM *vm{main::instance().vm()};

		desc.func(&string_table::script_find_index, "script_find_index"sv, "find"sv);
		desc.func(&string_table::script_num_strings, "script_num_strings"sv, "num"sv);
		desc.func(&string_table::script_get_string, "script_get_string"sv, "get"sv);
		desc.func(&string_table::script_add_string, "script_add_string"sv, "add"sv);

		if(!vm->RegisterClass(&desc)) {
			error("vmod: failed to register stringtable script class\n"sv);
			return false;
		}

		return true;
	}

	bool string_table::initialize() noexcept
	{
		gsdk::IScriptVM *vm{main::instance().vm()};

		instance = vm->RegisterInstance(&desc, this);
		if(!instance || instance == gsdk::INVALID_HSCRIPT) {
			return false;
		}

		//TODO!! SetInstanceUniqueID

		return true;
	}

	void string_table::unbindings() noexcept
	{
		
	}

	std::size_t string_table::script_find_index(std::string_view name) const noexcept
	{
		gsdk::IScriptVM *vm{main::instance().vm()};

		if(name.empty()) {
			vm->RaiseException("vmod: invalid name");
			return static_cast<std::size_t>(-1);
		}

		if(!table) {
			vm->RaiseException("vmod: stringtable is not created yet");
			return static_cast<std::size_t>(-1);
		}

		return static_cast<std::size_t>(table->FindStringIndex(name.data()));
	}

	std::size_t string_table::script_num_strings() const noexcept
	{
		gsdk::IScriptVM *vm{main::instance().vm()};

		if(!table) {
			vm->RaiseException("vmod: stringtable is not created yet");
			return static_cast<std::size_t>(-1);
		}

		return static_cast<std::size_t>(table->GetNumStrings());
	}

	std::string_view string_table::script_get_string(std::size_t i) const noexcept
	{
		gsdk::IScriptVM *vm{main::instance().vm()};

		if(i == static_cast<std::size_t>(-1) || static_cast<int>(i) >= table->GetNumStrings()) {
			vm->RaiseException("vmod: invalid index");
			return {};
		}

		if(!table) {
			vm->RaiseException("vmod: stringtable is not created yet");
			return {};
		}

		return table->GetString(static_cast<int>(i));
	}

	std::size_t string_table::script_add_string(std::string_view name, ssize_t bytes, const void *data) noexcept
	{
		gsdk::IScriptVM *vm{main::instance().vm()};

		if(name.empty()) {
			vm->RaiseException("vmod: invalid string");
			return static_cast<std::size_t>(-1);
		}

		if(!table) {
			vm->RaiseException("vmod: stringtable is not created yet");
			return static_cast<std::size_t>(-1);
		}

		return static_cast<std::size_t>(table->AddString(true, name.data(), bytes, data));
	}

	string_table::~string_table() noexcept
	{
		if(instance && instance != gsdk::INVALID_HSCRIPT) {
			main::instance().vm()->RemoveInstance(instance);
		}
	}
}
