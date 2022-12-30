#pragma once

#include "../vscript/vscript.hpp"
#include <cstddef>
#include <vector>
#include <string>
#include <filesystem>

namespace vmod::bindings::docs
{
	inline void ident(std::string &file, std::size_t num) noexcept
	{ file.insert(file.end(), num, '\t'); }

	extern void gen_date(std::string &file) noexcept;

	extern std::string_view get_class_desc_name(const gsdk::ScriptClassDesc_t *desc) noexcept;

	extern bool write(const gsdk::ScriptFunctionBinding_t *func, bool global, std::size_t ident, std::string &file, bool respect_hide) noexcept;
	extern bool write(const gsdk::ScriptClassDesc_t *desc, bool global, std::size_t ident, std::string &file, bool respect_hide) noexcept;
	extern void write(const std::filesystem::path &dir, const std::vector<const gsdk::ScriptClassDesc_t *> &vec, bool respect_hide) noexcept;
	extern void write(const std::filesystem::path &dir, const std::vector<const gsdk::ScriptFunctionBinding_t *> &vec, bool respect_hide) noexcept;

	enum class write_enum_how : unsigned char
	{
		flags,
		name,
		normal
	};
	extern void write(std::string &file, std::size_t depth, gsdk::HSCRIPT enum_table, write_enum_how how) noexcept;
}
