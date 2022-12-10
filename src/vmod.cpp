#include "vmod.hpp"
#include "symbol_cache.hpp"
#include "gsdk/engine/vsp.hpp"
#include "gsdk/tier0/dbg.hpp"
#include <cstring>

#include "plugin.hpp"
#include "filesystem.hpp"
#include "gsdk/server/gamerules.hpp"

#include <filesystem>
#include <string_view>
#include <climits>

#include <iostream>
#include "glob.h"

#include "convar.hpp"
#include <utility>

#include "gsdk/tier1/utlstring.hpp"
#include "yaml.hpp"
#include "ffi.hpp"

namespace vmod
{
#if __has_include("vmod_base.nut.h")
#ifdef __clang__
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#endif
	#include "vmod_base.nut.h"
#ifdef __clang__
	#pragma clang diagnostic pop
#endif
	#define __VMOD_BASE_SCRIPT_HEADER_INCLUDED
#endif
}

namespace vmod
{
	static void(gsdk::IScriptVM::*CreateArray)(gsdk::ScriptVariant_t &);
	static int(gsdk::IScriptVM::*GetArrayCount)(gsdk::HSCRIPT);
}

namespace gsdk
{
	int IScriptVM::GetArrayCount(HSCRIPT array)
	{
		return (this->*vmod::GetArrayCount)(array);
	}

	HSCRIPT IScriptVM::CreateArray() noexcept
	{
		ScriptVariant_t var;
		(this->*vmod::CreateArray)(var);
		return var.m_hScript;
	}
}

namespace vmod
{
	class vmod vmod;

	gsdk::HSCRIPT vmod::script_find_plugin(std::string_view name) noexcept
	{
		using namespace std::literals::string_view_literals;

		std::filesystem::path path{name};
		if(!path.is_absolute()) {
			path = (plugins_dir/path);
		}
		path.replace_extension(scripts_extension);

		for(auto it{plugins.begin()}; it != plugins.end(); ++it) {
			const std::filesystem::path &pl_path{static_cast<std::filesystem::path>(*(*it))};

			if(pl_path == path) {
				return (*it)->instance();
			}
		}

		return nullptr;
	}

	static singleton_class_desc_t<class vmod> vmod_desc{"__vmod_singleton_class"};

	inline class vmod &vmod::instance() noexcept
	{ return ::vmod::vmod; }

	bool vmod::Get(const gsdk::CUtlString &name, gsdk::ScriptVariant_t &value)
	{
		return vm_->GetValue(vs_instance_, name.c_str(), &value);
	}

	class lib_symbols_singleton
	{
	protected:
		lib_symbols_singleton() noexcept = default;

	public:
		virtual ~lib_symbols_singleton() noexcept;

	private:
		friend class vmod;

		static gsdk::HSCRIPT script_lookup_shared(symbol_cache::const_iterator it) noexcept;
		static gsdk::HSCRIPT script_lookup_shared(symbol_cache::qualification_info::const_iterator it) noexcept;

		struct script_qual_it_t final
		{
			inline ~script_qual_it_t() noexcept
			{
				if(this->instance && this->instance != gsdk::INVALID_HSCRIPT) {
					vmod.vm()->RemoveInstance(this->instance);
				}
			}

			gsdk::HSCRIPT script_lookup(std::string_view name) const noexcept
			{
				using namespace std::literals::string_view_literals;

				std::string name_tmp{name};

				auto tmp_it{it_->second.find(name_tmp)};
				if(tmp_it == it_->second.end()) {
					return nullptr;
				}

				return script_lookup_shared(tmp_it);
			}

			inline std::string_view script_name() const noexcept
			{ return it_->first; }

			inline void script_delete() noexcept
			{ delete this; }

			symbol_cache::const_iterator it_;
			gsdk::HSCRIPT instance{gsdk::INVALID_HSCRIPT};
		};

		struct script_name_it_t final
		{
			inline ~script_name_it_t() noexcept
			{
				if(this->instance && this->instance != gsdk::INVALID_HSCRIPT) {
					vmod.vm()->RemoveInstance(this->instance);
				}
			}

			gsdk::HSCRIPT script_lookup(std::string_view name) const noexcept
			{
				using namespace std::literals::string_view_literals;

				std::string name_tmp{name};

				auto tmp_it{it_->second.find(name_tmp)};
				if(tmp_it == it_->second.end()) {
					return nullptr;
				}

				return script_lookup_shared(tmp_it);
			}

			inline std::string_view script_name() const noexcept
			{ return it_->first; }
			inline void *script_addr() const noexcept
			{ return it_->second.addr<void *>(); }
			inline generic_func_t script_func() const noexcept
			{ return it_->second.func<generic_func_t>(); }
			inline generic_mfp_t script_mfp() const noexcept
			{ return it_->second.mfp<generic_mfp_t>(); }
			inline std::size_t script_size() const noexcept
			{ return it_->second.size(); }

			inline void script_delete() noexcept
			{ delete this; }

			symbol_cache::qualification_info::const_iterator it_;
			gsdk::HSCRIPT instance{gsdk::INVALID_HSCRIPT};
		};

		static inline class_desc_t<script_qual_it_t> qual_it_desc{"__vmod_sym_qual_it_class"};
		static inline class_desc_t<script_name_it_t> name_it_desc{"__vmod_sym_name_it_class"};

		virtual const symbol_cache &get_syms() const noexcept = 0;

		gsdk::HSCRIPT script_lookup(std::string_view name) const noexcept
		{
			using namespace std::literals::string_view_literals;

			std::string name_tmp{name};

			const symbol_cache &syms{get_syms()};

			auto it{syms.find(name_tmp)};
			if(it == syms.end()) {
				return nullptr;
			}

			return script_lookup_shared(it);
		}

		gsdk::HSCRIPT script_lookup_global(std::string_view name) const noexcept
		{
			using namespace std::literals::string_view_literals;

			std::string name_tmp{name};

			const symbol_cache::qualification_info &syms{get_syms().global()};

			auto it{syms.find(name_tmp)};
			if(it == syms.end()) {
				return nullptr;
			}

			return script_lookup_shared(it);
		}

		static bool bindings() noexcept;
		static void unbindings() noexcept;

		void unregister() noexcept
		{
			if(vs_instance_ && vs_instance_ != gsdk::INVALID_HSCRIPT) {
				vmod.vm()->RemoveInstance(vs_instance_);
			}
		}

	protected:
		gsdk::HSCRIPT vs_instance_{gsdk::INVALID_HSCRIPT};
	};

	lib_symbols_singleton::~lib_symbols_singleton() noexcept
	{
		
	}

	static singleton_class_desc_t<class lib_symbols_singleton> lib_symbols_desc{"__vmod_lib_symbols_class"};

	class server_symbols_singleton final : public lib_symbols_singleton
	{
	public:
		inline server_symbols_singleton() noexcept
		{
		}

		~server_symbols_singleton() noexcept override;

		const symbol_cache &get_syms() const noexcept override;

		static server_symbols_singleton &instance() noexcept;

		bool register_() noexcept
		{
			using namespace std::literals::string_view_literals;

			gsdk::IScriptVM *vm{vmod.vm()};

			vs_instance_ = vm->RegisterInstance(&lib_symbols_desc, this);
			if(!vs_instance_ || vs_instance_ == gsdk::INVALID_HSCRIPT) {
				error("vmod: failed to create server symbols instance\n"sv);
				return false;
			}

			vm->SetInstanceUniqeId(vs_instance_, "__vmod_server_symbols_singleton");

			if(!vm->SetValue(vmod.symbols_table(), "sv", vs_instance_)) {
				error("vmod: failed to set server symbols table value\n"sv);
				return false;
			}

			return true;
		}
	};

	server_symbols_singleton::~server_symbols_singleton() noexcept {}

	const symbol_cache &server_symbols_singleton::get_syms() const noexcept
	{ return vmod.server_lib.symbols(); }

	static class server_symbols_singleton server_symbols_singleton;

	inline class server_symbols_singleton &server_symbols_singleton::instance() noexcept
	{ return ::vmod::server_symbols_singleton; }

	gsdk::HSCRIPT lib_symbols_singleton::script_lookup_shared(symbol_cache::const_iterator it) noexcept
	{
		script_qual_it_t *script_it{new script_qual_it_t};
		script_it->it_ = it;

		gsdk::IScriptVM *vm{vmod.vm()};

		script_it->instance = vm->RegisterInstance(&qual_it_desc, script_it);
		if(!script_it->instance || script_it->instance == gsdk::INVALID_HSCRIPT) {
			delete script_it;
			vm->RaiseException("vmod: failed to create symbol qualification iterator instance");
			return nullptr;
		}

		//vm->SetInstanceUniqeId

		return script_it->instance;
	}

	gsdk::HSCRIPT lib_symbols_singleton::script_lookup_shared(symbol_cache::qualification_info::const_iterator it) noexcept
	{
		script_name_it_t *script_it{new script_name_it_t};
		script_it->it_ = it;

		gsdk::IScriptVM *vm{vmod.vm()};

		script_it->instance = vm->RegisterInstance(&name_it_desc, script_it);
		if(!script_it->instance || script_it->instance == gsdk::INVALID_HSCRIPT) {
			delete script_it;
			vm->RaiseException("vmod: failed to create symbol name iterator instance");
			return nullptr;
		}

		//vm->SetInstanceUniqeId

		return script_it->instance;
	}

	void lib_symbols_singleton::unbindings() noexcept
	{
		server_symbols_singleton.unregister();
	}

	bool lib_symbols_singleton::bindings() noexcept
	{
		using namespace std::literals::string_view_literals;

		gsdk::IScriptVM *vm{vmod.vm()};

		lib_symbols_desc.func(&lib_symbols_singleton::script_lookup, "__script_lookup"sv, "lookup"sv);
		lib_symbols_desc.func(&lib_symbols_singleton::script_lookup_global, "__script_lookup_global"sv, "lookup_global"sv);
		lib_symbols_desc.doc_class_name("symbol_cache"sv);

		if(!vm->RegisterClass(&lib_symbols_desc)) {
			error("vmod: failed to register lib symbols script class\n"sv);
			return false;
		}

		qual_it_desc.func(&script_qual_it_t::script_name, "__script_name"sv, "get_name"sv);
		qual_it_desc.func(&script_qual_it_t::script_lookup, "__script_lookup"sv, "lookup"sv);
		qual_it_desc.func(&script_qual_it_t::script_delete, "__script_delete"sv, "free"sv);
		qual_it_desc.dtor();
		qual_it_desc.doc_class_name("symbol_qualifier"sv);

		if(!vm->RegisterClass(&qual_it_desc)) {
			error("vmod: failed to register symbol qualification iterator script class\n"sv);
			return false;
		}

		name_it_desc.func(&script_name_it_t::script_name, "__script_name"sv, "get_name"sv);
		name_it_desc.func(&script_name_it_t::script_addr, "__script_addr"sv, "get_addr"sv);
		name_it_desc.func(&script_name_it_t::script_func, "__script_func"sv, "get_func"sv);
		name_it_desc.func(&script_name_it_t::script_mfp, "__script_mfp"sv, "get_mfp"sv);
		name_it_desc.func(&script_name_it_t::script_size, "__script_size"sv, "get_size"sv);
		name_it_desc.func(&script_name_it_t::script_lookup, "__script_lookup"sv, "lookup"sv);
		name_it_desc.func(&script_name_it_t::script_delete, "__script_delete"sv, "free"sv);
		name_it_desc.dtor();
		name_it_desc.doc_class_name("symbol_name"sv);

		if(!vm->RegisterClass(&name_it_desc)) {
			error("vmod: failed to register symbol name iterator script class\n"sv);
			return false;
		}

		if(!server_symbols_singleton.register_()) {
			return false;
		}

		return true;
	}

	class filesystem_singleton final : public gsdk::ISquirrelMetamethodDelegate
	{
	public:
		~filesystem_singleton() noexcept override;

		static filesystem_singleton &instance() noexcept;

		bool bindings() noexcept;
		void unbindings() noexcept;

	private:
		static int script_globerr(const char *epath, int eerrno)
		{
			//vmod.vm()->RaiseException("vmod: glob error %i on %s:", eerrno, epath);
			return 0;
		}

		static gsdk::HSCRIPT script_glob(std::filesystem::path pattern) noexcept
		{
			glob_t glob;
			if(::glob(pattern.c_str(), GLOB_ERR|GLOB_NOSORT, script_globerr, &glob) != 0) {
				globfree(&glob);
				return nullptr;
			}

			gsdk::IScriptVM *vm{vmod.vm()};

			gsdk::HSCRIPT arr{vm->CreateArray()};

			for(std::size_t i{0}; i < glob.gl_pathc; ++i) {
				std::string temp{glob.gl_pathv[i]};

				script_variant_t var;
				var.assign<std::string>(std::move(temp));
				vm->ArrayAddToTail(arr, std::move(var));
			}

			globfree(&glob);

			return arr;
		}

		static std::filesystem::path script_join_paths(const script_variant_t *va_args, std::size_t num_args, ...) noexcept
		{
			std::filesystem::path final_path;

			for(std::size_t i{0}; i < num_args; ++i) {
				final_path /= va_args[i].get<std::filesystem::path>();
			}

			return final_path;
		}

		bool Get(const gsdk::CUtlString &name, gsdk::ScriptVariant_t &value) override;

		gsdk::HSCRIPT vs_instance_{gsdk::INVALID_HSCRIPT};
		gsdk::HSCRIPT scope{gsdk::INVALID_HSCRIPT};
		gsdk::CSquirrelMetamethodDelegateImpl *get_impl{nullptr};
	};

	bool filesystem_singleton::Get(const gsdk::CUtlString &name, gsdk::ScriptVariant_t &value)
	{
		return vmod.vm()->GetValue(vs_instance_, name.c_str(), &value);
	}

	filesystem_singleton::~filesystem_singleton() {}

	static class filesystem_singleton filesystem_singleton;

	static singleton_class_desc_t<class filesystem_singleton> filesystem_singleton_desc{"__vmod_filesystem_singleton_class"};

	inline class filesystem_singleton &filesystem_singleton::instance() noexcept
	{ return ::vmod::filesystem_singleton; }

	bool filesystem_singleton::bindings() noexcept
	{
		using namespace std::literals::string_view_literals;

		gsdk::IScriptVM *vm{vmod.vm()};

		filesystem_singleton_desc.func(&filesystem_singleton::script_join_paths, "__script_join_paths"sv, "join_paths"sv);
		filesystem_singleton_desc.func(&filesystem_singleton::script_glob, "__script_glob"sv, "glob"sv);

		if(!vm->RegisterClass(&filesystem_singleton_desc)) {
			error("vmod: failed to register vmod filesystem script class\n"sv);
			return false;
		}

		vs_instance_ = vm->RegisterInstance(&filesystem_singleton_desc, this);
		if(!vs_instance_ || vs_instance_ == gsdk::INVALID_HSCRIPT) {
			error("vmod: failed to create vmod filesystem instance\n"sv);
			return false;
		}

		vm->SetInstanceUniqeId(vs_instance_, "__vmod_filesystem_singleton");

		scope = vm->CreateScope("__vmod_fs_scope", nullptr);
		if(!scope || scope == gsdk::INVALID_HSCRIPT) {
			error("vmod: failed to create filesystem scope\n"sv);
			return false;
		}

		gsdk::HSCRIPT vmod_scope{vmod.scope()};
		if(!vm->SetValue(vmod_scope, "fs", scope)) {
			error("vmod: failed to set filesystem scope value\n"sv);
			return false;
		}

		if(!vm->SetValue(scope, "game_dir", vmod.game_dir().c_str())) {
			error("vmod: failed to set game dir value\n"sv);
			return false;
		}

		get_impl = vm->MakeSquirrelMetamethod_Get(vmod_scope, "fs", this, false);
		if(!get_impl) {
			error("vmod: failed to create filesystem _get metamethod\n"sv);
			return false;
		}

		return true;
	}

	void filesystem_singleton::unbindings() noexcept
	{
		gsdk::IScriptVM *vm{vmod.vm()};

		if(vs_instance_ && vs_instance_ != gsdk::INVALID_HSCRIPT) {
			vm->RemoveInstance(vs_instance_);
		}

		if(get_impl) {
			vm->DestroySquirrelMetamethod_Get(get_impl);
		}

		if(vm->ValueExists(scope, "game_dir")) {
			vm->ClearValue(scope, "game_dir");
		}

		if(scope && scope != gsdk::INVALID_HSCRIPT) {
			vm->ReleaseScope(scope);
		}

		gsdk::HSCRIPT vmod_scope{vmod.scope()};
		if(vm->ValueExists(vmod_scope, "fs")) {
			vm->ClearValue(vmod_scope, "fs");
		}
	}

	class_desc_t<vmod::script_stringtable> vmod::stringtable_desc{"__vmod_stringtable_class"};

	static gsdk::INetworkStringTable *m_pDownloadableFileTable;
	static gsdk::INetworkStringTable *m_pModelPrecacheTable;
	static gsdk::INetworkStringTable *m_pGenericPrecacheTable;
	static gsdk::INetworkStringTable *m_pSoundPrecacheTable;
	static gsdk::INetworkStringTable *m_pDecalPrecacheTable;

	static gsdk::INetworkStringTable *g_pStringTableParticleEffectNames;
	static gsdk::INetworkStringTable *g_pStringTableEffectDispatch;
	static gsdk::INetworkStringTable *g_pStringTableVguiScreen;
	static gsdk::INetworkStringTable *g_pStringTableMaterials;
	static gsdk::INetworkStringTable *g_pStringTableInfoPanel;
	static gsdk::INetworkStringTable *g_pStringTableClientSideChoreoScenes;

	std::size_t vmod::script_stringtable::script_find_index(std::string_view name) const noexcept
	{
		gsdk::IScriptVM *vm{::vmod::vmod.vm()};

		if(!table) {
			vm->RaiseException("vmod: stringtable is not created yet");
			return static_cast<std::size_t>(-1);
		}

		return static_cast<std::size_t>(table->FindStringIndex(name.data()));
	}

	std::size_t vmod::script_stringtable::script_num_strings() const noexcept
	{
		gsdk::IScriptVM *vm{::vmod::vmod.vm()};

		if(!table) {
			vm->RaiseException("vmod: stringtable is not created yet");
			return static_cast<std::size_t>(-1);
		}

		return static_cast<std::size_t>(table->GetNumStrings());
	}

	std::string_view vmod::script_stringtable::script_get_string(std::size_t i) const noexcept
	{
		gsdk::IScriptVM *vm{::vmod::vmod.vm()};

		if(!table) {
			vm->RaiseException("vmod: stringtable is not created yet");
			return {};
		}

		return table->GetString(static_cast<int>(i));
	}

	std::size_t vmod::script_stringtable::script_add_string(std::string_view name, ssize_t bytes, const void *data) noexcept
	{
		gsdk::IScriptVM *vm{::vmod::vmod.vm()};

		if(!table) {
			vm->RaiseException("vmod: stringtable is not created yet");
			return static_cast<std::size_t>(-1);
		}

		return static_cast<std::size_t>(table->AddString(true, name.data(), static_cast<ssize_t>(bytes), data));
	}

	vmod::script_stringtable::~script_stringtable() noexcept
	{
		if(instance && instance != gsdk::INVALID_HSCRIPT) {
			::vmod::vmod.vm()->RemoveInstance(instance);
		}
	}

	void vmod::stringtables_removed() noexcept
	{
		are_string_tables_created = false;

		for(const auto &it : script_stringtables) {
			it.second->table = nullptr;
		}
	}

	void vmod::recreate_script_stringtables() noexcept
	{
		are_string_tables_created = true;

		using namespace std::literals::string_literals;

		static auto set_table_value{
			[this](std::string &&name, gsdk::INetworkStringTable *value) noexcept -> void {
				auto it{script_stringtables.find(name)};
				if(it != script_stringtables.end()) {
					it->second->table = value;
				}
			}
		};

		set_table_value(gsdk::DOWNLOADABLE_FILE_TABLENAME, m_pDownloadableFileTable);
		set_table_value(gsdk::MODEL_PRECACHE_TABLENAME, m_pModelPrecacheTable);
		set_table_value(gsdk::GENERIC_PRECACHE_TABLENAME, m_pGenericPrecacheTable);
		set_table_value(gsdk::SOUND_PRECACHE_TABLENAME, m_pSoundPrecacheTable);
		set_table_value(gsdk::DECAL_PRECACHE_TABLENAME, m_pDecalPrecacheTable);

		set_table_value("ParticleEffectNames"s, g_pStringTableParticleEffectNames);
		set_table_value("EffectDispatch"s, g_pStringTableEffectDispatch);
		set_table_value("VguiScreen"s, g_pStringTableVguiScreen);
		set_table_value("Materials"s, g_pStringTableMaterials);
		set_table_value("InfoPanel"s, g_pStringTableInfoPanel);
		set_table_value("Scenes"s, g_pStringTableClientSideChoreoScenes);

		for(const auto &pl : plugins) {
			if(!*pl) {
				continue;
			}

			pl->string_tables_created();
		}
	}

	bool vmod::create_script_stringtable(std::string &&name) noexcept
	{
		using namespace std::literals::string_view_literals;

		std::unique_ptr<script_stringtable> ptr{new script_stringtable};

		gsdk::HSCRIPT table_instance{vm_->RegisterInstance(&stringtable_desc, ptr.get())};
		if(!table_instance || table_instance == gsdk::INVALID_HSCRIPT) {
			error("vmod: failed to create stringtable '%s' instance\n"sv);
			return false;
		}

		if(!vm_->SetValue(stringtable_table, name.c_str(), table_instance)) {
			vm_->RemoveInstance(table_instance);
			error("vmod: failed to set stringtable '%s' value\n"sv);
			return false;
		}

		ptr->instance = table_instance;
		ptr->table = nullptr;

		script_stringtables.emplace(std::move(name), std::move(ptr));

		return true;
	}

	class cvar_singleton : public gsdk::ISquirrelMetamethodDelegate
	{
	public:
		~cvar_singleton() noexcept override;

		static cvar_singleton &instance() noexcept;

		bool bindings() noexcept;
		void unbindings() noexcept;

	private:
		friend class vmod;

		class script_cvar final : public plugin::owned_instance
		{
		public:
			~script_cvar() noexcept override;

		private:
			friend class cvar_singleton;

			inline void script_delete() noexcept
			{ delete this; }

			inline int script_get_value_int() const noexcept
			{ return var->ConVar::GetInt(); }
			inline float script_get_value_float() const noexcept
			{ return var->ConVar::GetFloat(); }
			inline std::string_view script_get_value_string() const noexcept
			{ return var->ConVar::GetString(); }
			inline bool script_get_value_bool() const noexcept
			{ return var->ConVar::GetBool(); }

			inline void script_set_value_int(int value) const noexcept
			{ var->ConVar::SetValue(value); }
			inline void script_set_value_float(float value) const noexcept
			{ var->ConVar::SetValue(value); }
			inline void script_set_value_string(std::string_view value) const noexcept
			{ var->ConVar::SetValue(value.data()); }
			inline void script_set_value_bool(bool value) const noexcept
			{ var->ConVar::SetValue(value); }

			inline void script_set_value(script_variant_t value) const noexcept
			{
				switch(value.m_type) {
					case gsdk::FIELD_CSTRING:
					var->ConVar::SetValue(value.m_pszString);
					break;
					case gsdk::FIELD_INTEGER:
					var->ConVar::SetValue(value.m_int);
					break;
					case gsdk::FIELD_FLOAT:
					var->ConVar::SetValue(value.m_float);
					break;
					case gsdk::FIELD_BOOLEAN:
					var->ConVar::SetValue(value.m_bool);
					break;
					default:
					vmod.vm()->RaiseException("vmod: invalid type");
					break;
				}
			}

			inline script_variant_t script_get_value() const noexcept
			{
				const char *str{var->InternalGetString()};
				std::size_t len{var->InternalGetStringLength()};

				if(std::strncmp(str, "true", len) == 0) {
					return true;
				} else if(std::strncmp(str, "false", len) == 0) {
					return false;
				} else {
					bool is_float{false};

					for(std::size_t i{0}; i < len; ++i) {
						if(str[i] == '.') {
							is_float = true;
							continue;
						}

						if(!std::isdigit(str[i])) {
							return std::string_view{str};
						}
					}

					if(is_float) {
						return var->GetFloat();
					} else {
						return var->GetInt();
					}
				}
			}

			gsdk::ConVar *var;
			bool free_var;
			gsdk::HSCRIPT instance{gsdk::INVALID_HSCRIPT};
		};

		static inline class_desc_t<script_cvar> script_cvar_desc{"__vmod_script_cvar_class"};

		static gsdk::HSCRIPT script_create_cvar(std::string_view name, std::string_view value) noexcept
		{
			gsdk::IScriptVM *vm{vmod.vm()};

			if(cvar->FindCommandBase(name.data()) != nullptr) {
				vm->RaiseException("vmod: name already in use");
				return nullptr;
			}

			script_cvar *svar{new script_cvar};

			gsdk::HSCRIPT cvar_instance{vm->RegisterInstance(&script_cvar_desc, svar)};
			if(!cvar_instance || cvar_instance == gsdk::INVALID_HSCRIPT) {
				delete svar;
				vm->RaiseException("vmod: failed to register instance");
				return nullptr;
			}

			ConVar *var{new ConVar};
			var->initialize(name, value);

			svar->var = var;
			svar->free_var = true;
			svar->instance = cvar_instance;

			svar->set_plugin();

			return svar->instance;
		}

		static gsdk::HSCRIPT script_find_cvar(std::string_view name) noexcept
		{
			gsdk::IScriptVM *vm{vmod.vm()};

			gsdk::ConVar *var{cvar->FindVar(name.data())};
			if(!var) {
				return nullptr;
			}

			script_cvar *svar{new script_cvar};

			gsdk::HSCRIPT cvar_instance{vm->RegisterInstance(&script_cvar_desc, svar)};
			if(!cvar_instance || cvar_instance == gsdk::INVALID_HSCRIPT) {
				delete svar;
				vm->RaiseException("vmod: failed to register instance");
				return nullptr;
			}

			svar->var = var;
			svar->free_var = false;
			svar->instance = cvar_instance;

			svar->set_plugin();

			return svar->instance;
		}

		bool Get(const gsdk::CUtlString &name, gsdk::ScriptVariant_t &value) override;

		gsdk::HSCRIPT flags_table{gsdk::INVALID_HSCRIPT};

		gsdk::HSCRIPT vs_instance_{gsdk::INVALID_HSCRIPT};
		gsdk::HSCRIPT scope{gsdk::INVALID_HSCRIPT};
		gsdk::CSquirrelMetamethodDelegateImpl *get_impl{nullptr};
	};

	bool cvar_singleton::Get(const gsdk::CUtlString &name, gsdk::ScriptVariant_t &value)
	{
		return vmod.vm()->GetValue(vs_instance_, name.c_str(), &value);
	}

	cvar_singleton::~cvar_singleton() {}

	static class cvar_singleton cvar_singleton;

	static singleton_class_desc_t<class cvar_singleton> cvar_singleton_desc{"__vmod_cvar_singleton_class"};

	inline class cvar_singleton &cvar_singleton::instance() noexcept
	{ return ::vmod::cvar_singleton; }

	cvar_singleton::script_cvar::~script_cvar() noexcept
	{
		if(free_var) {
			delete var;
		}

		if(instance && instance != gsdk::INVALID_HSCRIPT) {
			vmod.vm()->RemoveInstance(instance);
		}
	}

	bool cvar_singleton::bindings() noexcept
	{
		using namespace std::literals::string_view_literals;

		gsdk::IScriptVM *vm{vmod.vm()};

		cvar_singleton_desc.func(&cvar_singleton::script_create_cvar, "__script_create_cvar"sv, "create_var"sv);
		cvar_singleton_desc.func(&cvar_singleton::script_find_cvar, "__script_find_cvar"sv, "find_var"sv);

		if(!vm->RegisterClass(&cvar_singleton_desc)) {
			error("vmod: failed to register vmod cvar singleton script class\n"sv);
			return false;
		}

		script_cvar_desc.func(&script_cvar::script_set_value, "__script_script_set_value"sv, "set"sv);
		script_cvar_desc.func(&script_cvar::script_set_value_string, "__script_set_value_string"sv, "set_string"sv);
		script_cvar_desc.func(&script_cvar::script_set_value_float, "__script_set_value_float"sv, "set_float"sv);
		script_cvar_desc.func(&script_cvar::script_set_value_int, "__script_set_value_int"sv, "set_int"sv);
		script_cvar_desc.func(&script_cvar::script_set_value_bool, "__script_set_value_bool"sv, "set_bool"sv);
		script_cvar_desc.func(&script_cvar::script_get_value, "__script_script_get_value"sv, "get"sv);
		script_cvar_desc.func(&script_cvar::script_get_value_string, "__script_get_value_string"sv, "get_string"sv);
		script_cvar_desc.func(&script_cvar::script_get_value_float, "__script_get_value_float"sv, "get_float"sv);
		script_cvar_desc.func(&script_cvar::script_get_value_int, "__script_get_value_int"sv, "get_int"sv);
		script_cvar_desc.func(&script_cvar::script_get_value_bool, "__script_get_value_bool"sv, "get_bool"sv);
		script_cvar_desc.func(&script_cvar::script_delete, "__script_delete"sv, "free"sv);
		script_cvar_desc.dtor();
		script_cvar_desc.doc_class_name("convar"sv);

		if(!vm->RegisterClass(&script_cvar_desc)) {
			error("vmod: failed to register vmod cvar script class\n"sv);
			return false;
		}

		vs_instance_ = vm->RegisterInstance(&cvar_singleton_desc, this);
		if(!vs_instance_ || vs_instance_ == gsdk::INVALID_HSCRIPT) {
			error("vmod: failed to create vmod cvar singleton instance\n"sv);
			return false;
		}

		vm->SetInstanceUniqeId(vs_instance_, "__vmod_cvar_singleton");

		scope = vm->CreateScope("__vmod_cvar_scope", nullptr);
		if(!scope || scope == gsdk::INVALID_HSCRIPT) {
			error("vmod: failed to create cvar scope\n"sv);
			return false;
		}

		gsdk::HSCRIPT vmod_scope{vmod.scope()};
		if(!vm->SetValue(vmod_scope, "cvar", scope)) {
			error("vmod: failed to set cvar scope value\n"sv);
			return false;
		}

		get_impl = vm->MakeSquirrelMetamethod_Get(vmod_scope, "cvar", this, false);
		if(!get_impl) {
			error("vmod: failed to create cvar _get metamethod\n"sv);
			return false;
		}

		flags_table = vm->CreateTable();
		if(!flags_table || flags_table == gsdk::INVALID_HSCRIPT) {
			error("vmod: failed to create cvar flags table\n"sv);
			return false;
		}

		if(!vm->SetValue(scope, "flags", flags_table)) {
			error("vmod: failed to set cvar flags table value\n"sv);
			return false;
		}

		{
			if(!vm->SetValue(flags_table, "none", script_variant_t{gsdk::FCVAR_NONE})) {
				error("vmod: failed to set cvar none flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "development", script_variant_t{gsdk::FCVAR_DEVELOPMENTONLY})) {
				error("vmod: failed to set cvar development flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "hidden", script_variant_t{gsdk::FCVAR_HIDDEN})) {
				error("vmod: failed to set cvar hidden flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "protected", script_variant_t{gsdk::FCVAR_PROTECTED})) {
				error("vmod: failed to set cvar protected flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "singleplayer", script_variant_t{gsdk::FCVAR_SPONLY})) {
				error("vmod: failed to set cvar singleplayer flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "printable_only", script_variant_t{gsdk::FCVAR_PRINTABLEONLY})) {
				error("vmod: failed to set cvar printable_only flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "never_string", script_variant_t{gsdk::FCVAR_NEVER_AS_STRING})) {
				error("vmod: failed to set cvar never_string flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "server", script_variant_t{gsdk::FCVAR_GAMEDLL})) {
				error("vmod: failed to set cvar server flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "client", script_variant_t{gsdk::FCVAR_CLIENTDLL})) {
				error("vmod: failed to set cvar client flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "archive", script_variant_t{gsdk::FCVAR_ARCHIVE})) {
				error("vmod: failed to set cvar archive flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "notify", script_variant_t{gsdk::FCVAR_NOTIFY})) {
				error("vmod: failed to set cvar notify flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "userinfo", script_variant_t{gsdk::FCVAR_USERINFO})) {
				error("vmod: failed to set cvar userinfo flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "cheat", script_variant_t{gsdk::FCVAR_CHEAT})) {
				error("vmod: failed to set cvar cheat flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "replicated", script_variant_t{gsdk::FCVAR_REPLICATED})) {
				error("vmod: failed to set cvar replicated flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "only_unconnected", script_variant_t{gsdk::FCVAR_NOT_CONNECTED})) {
				error("vmod: failed to set cvar only_unconnected flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "allowed_in_competitive", script_variant_t{gsdk::FCVAR_ALLOWED_IN_COMPETITIVE})) {
				error("vmod: failed to set cvar allowed_in_competitive flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "internal", script_variant_t{gsdk::FCVAR_INTERNAL_USE})) {
				error("vmod: failed to set cvar internal flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "server_can_exec", script_variant_t{gsdk::FCVAR_SERVER_CAN_EXECUTE})) {
				error("vmod: failed to set cvar server_can_exec flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "server_cant_query", script_variant_t{gsdk::FCVAR_SERVER_CANNOT_QUERY})) {
				error("vmod: failed to set cvar server_cant_query flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "client_can_exec", script_variant_t{gsdk::FCVAR_CLIENTCMD_CAN_EXECUTE})) {
				error("vmod: failed to set cvar client_can_exec flag value\n"sv);
				return false;
			}

			if(!vm->SetValue(flags_table, "exec_in_default", script_variant_t{gsdk::FCVAR_EXEC_DESPITE_DEFAULT})) {
				error("vmod: failed to set cvar exec_in_default flag value\n"sv);
				return false;
			}
		}

		return true;
	}

	void cvar_singleton::unbindings() noexcept
	{
		gsdk::IScriptVM *vm{vmod.vm()};

		if(vs_instance_ && vs_instance_ != gsdk::INVALID_HSCRIPT) {
			vm->RemoveInstance(vs_instance_);
		}

		if(get_impl) {
			vm->DestroySquirrelMetamethod_Get(get_impl);
		}

		if(flags_table && flags_table != gsdk::INVALID_HSCRIPT) {
			vm->ReleaseTable(flags_table);
		}

		if(vm->ValueExists(scope, "flags")) {
			vm->ClearValue(scope, "flags");
		}

		if(scope && scope != gsdk::INVALID_HSCRIPT) {
			vm->ReleaseScope(scope);
		}

		gsdk::HSCRIPT vmod_scope{vmod.scope()};
		if(vm->ValueExists(vmod_scope, "cvar")) {
			vm->ClearValue(vmod_scope, "cvar");
		}
	}

	bool vmod::bindings() noexcept
	{
		using namespace std::literals::string_view_literals;
		using namespace std::literals::string_literals;

		vmod_desc.func(&vmod::script_find_plugin, "__script_find_plugin"sv, "find_plugin"sv);

		if(!vm_->RegisterClass(&vmod_desc)) {
			error("vmod: failed to register vmod script class\n"sv);
			return false;
		}

		vs_instance_ = vm_->RegisterInstance(&vmod_desc, this);
		if(!vs_instance_ || vs_instance_ == gsdk::INVALID_HSCRIPT) {
			error("vmod: failed to create vmod instance\n"sv);
			return false;
		}

		vm_->SetInstanceUniqeId(vs_instance_, "__vmod_singleton");

		plugins_table_ = vm_->CreateTable();
		if(!plugins_table_ || plugins_table_ == gsdk::INVALID_HSCRIPT) {
			error("vmod: failed to create plugins table\n"sv);
			return false;
		}

		if(!vm_->SetValue(scope_, "plugins", plugins_table_)) {
			error("vmod: failed to set plugins table value\n"sv);
			return false;
		}

		symbols_table_ = vm_->CreateTable();
		if(!symbols_table_ || symbols_table_ == gsdk::INVALID_HSCRIPT) {
			error("vmod: failed to create symbols table\n"sv);
			return false;
		}

		if(!vm_->SetValue(scope_, "syms", symbols_table_)) {
			error("vmod: failed to set symbols table value\n"sv);
			return false;
		}

		stringtable_table = vm_->CreateTable();
		if(!stringtable_table || stringtable_table == gsdk::INVALID_HSCRIPT) {
			error("vmod: failed to create stringtable table\n"sv);
			return false;
		}

		if(!vm_->SetValue(scope_, "strtables", stringtable_table)) {
			error("vmod: failed to set stringtable table value\n"sv);
			return false;
		}

		stringtable_desc.func(&script_stringtable::script_find_index, "__script_find_index"sv, "find"sv);
		stringtable_desc.func(&script_stringtable::script_num_strings, "__script_num_strings"sv, "num"sv);
		stringtable_desc.func(&script_stringtable::script_get_string, "__script_get_string"sv, "get"sv);
		stringtable_desc.func(&script_stringtable::script_add_string, "__script_add_string"sv, "add"sv);
		stringtable_desc.doc_class_name("string_table"sv);

		if(!vm_->RegisterClass(&stringtable_desc)) {
			error("vmod: failed to register stringtable script class\n"sv);
			return false;
		}

		{
			if(!create_script_stringtable(gsdk::DOWNLOADABLE_FILE_TABLENAME)) {
				return false;
			}

			if(!create_script_stringtable(gsdk::MODEL_PRECACHE_TABLENAME)) {
				return false;
			}

			if(!create_script_stringtable(gsdk::GENERIC_PRECACHE_TABLENAME)) {
				return false;
			}

			if(!create_script_stringtable(gsdk::SOUND_PRECACHE_TABLENAME)) {
				return false;
			}

			if(!create_script_stringtable(gsdk::DECAL_PRECACHE_TABLENAME)) {
				return false;
			}

			if(!create_script_stringtable("ParticleEffectNames"s)) {
				return false;
			}

			if(!create_script_stringtable("EffectDispatch"s)) {
				return false;
			}

			if(!create_script_stringtable("VguiScreen"s)) {
				return false;
			}

			if(!create_script_stringtable("Materials"s)) {
				return false;
			}

			if(!create_script_stringtable("InfoPanel"s)) {
				return false;
			}

			if(!create_script_stringtable("Scenes"s)) {
				return false;
			}
		}

		if(!server_symbols_singleton.bindings()) {
			return false;
		}

		if(!filesystem_singleton.bindings()) {
			return false;
		}

		if(!cvar_singleton.bindings()) {
			return false;
		}

		get_impl = vm_->MakeSquirrelMetamethod_Get(nullptr, "vmod", this, false);
		if(!get_impl) {
			error("vmod: failed to create vmod _get metamethod\n"sv);
			return false;
		}

		if(!vm_->SetValue(scope_, "root_dir", root_dir.c_str())) {
			error("vmod: failed to set root dir value\n"sv);
			return false;
		}

		if(!plugin::bindings()) {
			return false;
		}

		if(!yaml::bindings()) {
			return false;
		}

		if(!ffi_bindings()) {
			return false;
		}

		return true;
	}

	bool vmod::binding_mods() noexcept
	{
		return true;
	}

	void vmod::unbindings() noexcept
	{
		plugin::unbindings();

		yaml::unbindings();

		server_symbols_singleton.unbindings();

		filesystem_singleton.unbindings();

		cvar_singleton.unbindings();

		ffi_unbindings();

		if(plugins_table_ && plugins_table_ != gsdk::INVALID_HSCRIPT) {
			vm_->ReleaseTable(plugins_table_);
		}

		if(vm_->ValueExists(scope_, "plugins")) {
			vm_->ClearValue(scope_, "plugins");
		}

		if(symbols_table_ && symbols_table_ != gsdk::INVALID_HSCRIPT) {
			vm_->ReleaseTable(symbols_table_);
		}

		if(vm_->ValueExists(scope_, "syms")) {
			vm_->ClearValue(scope_, "syms");
		}

		script_stringtables.clear();

		if(stringtable_table && stringtable_table != gsdk::INVALID_HSCRIPT) {
			vm_->ReleaseTable(stringtable_table);
		}

		if(vm_->ValueExists(scope_, "strtables")) {
			vm_->ClearValue(scope_, "strtables");
		}

		if(vm_->ValueExists(scope_, "cvar")) {
			vm_->ClearValue(scope_, "cvar");
		}

		if(vs_instance_ && vs_instance_ != gsdk::INVALID_HSCRIPT) {
			vm_->RemoveInstance(vs_instance_);
		}

		if(get_impl) {
			vm_->DestroySquirrelMetamethod_Get(get_impl);
		}

		if(vm_->ValueExists(scope_, "root_dir")) {
			vm_->ClearValue(scope_, "root_dir");
		}
	}

	static const unsigned char *g_Script_init;
	static const unsigned char *g_Script_vscript_server;
	static gsdk::IScriptVM **g_pScriptVM;
	static bool(*VScriptServerInit)();
	static void(*VScriptServerTerm)();
	static bool(*VScriptRunScript)(const char *, gsdk::HSCRIPT, bool);
	static void(gsdk::CTFGameRules::*RegisterScriptFunctions)();
	static void(*PrintFunc)(HSQUIRRELVM, const SQChar *, ...);
	static void(*ErrorFunc)(HSQUIRRELVM, const SQChar *, ...);
	static void(gsdk::IScriptVM::*RegisterFunctionGuts)(gsdk::ScriptFunctionBinding_t *, gsdk::ScriptClassDesc_t *);
	static SQRESULT(*sq_setparamscheck)(HSQUIRRELVM, SQInteger, const SQChar *);
	static gsdk::ScriptClassDesc_t **sv_classdesc_pHead;

	static bool in_vscript_server_init;
	static bool in_vscript_print;
	static bool in_vscript_error;

	static void vscript_output(const char *txt)
	{
		using namespace std::literals::string_view_literals;

		info("%s"sv, txt);
	}

	static gsdk::ScriptErrorFunc_t server_vs_error_cb;
	static bool vscript_error_output(gsdk::ScriptErrorLevel_t lvl, const char *txt)
	{
		using namespace std::literals::string_view_literals;

		in_vscript_error = true;
		bool ret{server_vs_error_cb(lvl, txt)};
		in_vscript_error = false;

		return ret;
	}

	static gsdk::SpewOutputFunc_t old_spew;
	static gsdk::SpewRetval_t new_spew(gsdk::SpewType_t type, const char *str)
	{
		if(in_vscript_error || in_vscript_print || in_vscript_server_init) {
			switch(type) {
				case gsdk::SPEW_LOG: {
					return gsdk::SPEW_CONTINUE;
				}
				case gsdk::SPEW_WARNING: {
					if(in_vscript_print) {
						return gsdk::SPEW_CONTINUE;
					}
				} break;
				default: break;
			}
		}

		const gsdk::Color *clr{GetSpewOutputColor()};

		if(!clr || (clr && (clr->r == 255 && clr->g == 255 && clr->b == 255))) {
			switch(type) {
				case gsdk::SPEW_MESSAGE: {
					if(in_vscript_error) {
						clr = &error_clr;
					} else {
						clr = &print_clr;
					}
				} break;
				case gsdk::SPEW_WARNING: {
					clr = &warning_clr;
				} break;
				case gsdk::SPEW_ASSERT: {
					clr = &error_clr;
				} break;
				case gsdk::SPEW_ERROR: {
					clr = &error_clr;
				} break;
				case gsdk::SPEW_LOG: {
					clr = &info_clr;
				} break;
				default: break;
			}
		}

		if(clr) {
			std::printf("\033[38;2;%hhu;%hhu;%hhum", clr->r, clr->g, clr->b);
			std::fflush(stdout);
		}

		gsdk::SpewRetval_t ret{old_spew(type, str)};

		std::fputs("\033[0m", stdout);
		std::fflush(stdout);

		return ret;
	}

	static bool vscript_server_init_called;

	static bool in_vscript_server_term;
	static gsdk::IScriptVM *(gsdk::IScriptManager::*CreateVM_original)(gsdk::ScriptLanguage_t);
	static gsdk::IScriptVM *CreateVM_detour_callback(gsdk::IScriptManager *pthis, gsdk::ScriptLanguage_t lang) noexcept
	{
		if(in_vscript_server_init) {
			gsdk::IScriptVM *vmod_vm{vmod.vm()};
			if(lang == vmod_vm->GetLanguage()) {
				return vmod_vm;
			} else {
				return nullptr;
			}
		}

		return (pthis->*CreateVM_original)(lang);
	}

	static void(gsdk::IScriptManager::*DestroyVM_original)(gsdk::IScriptVM *);
	static void DestroyVM_detour_callback(gsdk::IScriptManager *pthis, gsdk::IScriptVM *vm) noexcept
	{
		if(in_vscript_server_term) {
			if(vm == vmod.vm()) {
				return;
			}
		}

		(pthis->*DestroyVM_original)(vm);
	}

	static gsdk::ScriptStatus_t(gsdk::IScriptVM::*Run_original)(const char *, bool);
	static gsdk::ScriptStatus_t Run_detour_callback(gsdk::IScriptVM *pthis, const char *script, bool wait) noexcept
	{
		if(in_vscript_server_init) {
			if(script == reinterpret_cast<const char *>(g_Script_vscript_server)) {
				return gsdk::SCRIPT_DONE;
			}
		}

		return (pthis->*Run_original)(script, wait);
	}

	static detour<decltype(VScriptRunScript)> VScriptRunScript_detour;
	static bool VScriptRunScript_detour_callback(const char *script, gsdk::HSCRIPT scope, bool warn) noexcept
	{
		if(!vscript_server_init_called) {
			if(std::strcmp(script, "mapspawn") == 0) {
				return true;
			}
		}

		return VScriptRunScript_detour(script, scope, warn);
	}

	static detour<decltype(VScriptServerInit)> VScriptServerInit_detour;
	static bool VScriptServerInit_detour_callback() noexcept
	{
		in_vscript_server_init = true;
		bool ret{vscript_server_init_called ? true : VScriptServerInit_detour()};
		*g_pScriptVM = vmod.vm();
		if(vscript_server_init_called) {
			VScriptRunScript_detour("mapspawn", nullptr, false);
		}
		in_vscript_server_init = false;
		return ret;
	}

	static detour<decltype(VScriptServerTerm)> VScriptServerTerm_detour;
	static void VScriptServerTerm_detour_callback() noexcept
	{
		in_vscript_server_term = true;
		//VScriptServerTerm_detour();
		*g_pScriptVM = vmod.vm();
		in_vscript_server_term = false;
	}

	static char __vscript_printfunc_buffer[2048];
	static detour<decltype(PrintFunc)> PrintFunc_detour;
	static void PrintFunc_detour_callback(HSQUIRRELVM m_hVM, const SQChar *s, ...)
	{
		va_list varg_list;
		va_start(varg_list, s);
	#ifdef __clang__
		#pragma clang diagnostic push
		#pragma clang diagnostic ignored "-Wformat-nonliteral"
	#endif
		std::vsnprintf(__vscript_printfunc_buffer, sizeof(__vscript_printfunc_buffer), s, varg_list);
	#ifdef __clang__
		#pragma clang diagnostic pop
	#endif
		in_vscript_print = true;
		PrintFunc_detour(m_hVM, "%s", __vscript_printfunc_buffer);
		in_vscript_print = false;
		va_end(varg_list);
	}

	static char __vscript_errorfunc_buffer[2048];
	static detour<decltype(ErrorFunc)> ErrorFunc_detour;
	static void ErrorFunc_detour_callback(HSQUIRRELVM m_hVM, const SQChar *s, ...)
	{
		va_list varg_list;
		va_start(varg_list, s);
	#ifdef __clang__
		#pragma clang diagnostic push
		#pragma clang diagnostic ignored "-Wformat-nonliteral"
	#endif
		std::vsnprintf(__vscript_errorfunc_buffer, sizeof(__vscript_errorfunc_buffer), s, varg_list);
	#ifdef __clang__
		#pragma clang diagnostic pop
	#endif
		in_vscript_error = true;
		ErrorFunc_detour(m_hVM, "%s", __vscript_errorfunc_buffer);
		in_vscript_error = false;
		va_end(varg_list);
	}

	static gsdk::ScriptFunctionBinding_t *current_binding;

	static detour<decltype(RegisterFunctionGuts)> RegisterFunctionGuts_detour;
	static void RegisterFunctionGuts_detour_callback(gsdk::IScriptVM *vm, gsdk::ScriptFunctionBinding_t *binding, gsdk::ScriptClassDesc_t *classdesc)
	{
		current_binding = binding;

		RegisterFunctionGuts_detour(vm, binding, classdesc);

		if(binding->m_flags & func_desc_t::SF_VA_FUNC) {
			constexpr std::size_t arglimit{14};
			constexpr std::size_t va_args{arglimit};

			std::size_t current_size{binding->m_desc.m_Parameters.size()};
			if(current_size < arglimit) {
				std::size_t new_size{current_size + va_args};
				if(new_size > arglimit) {
					new_size = arglimit;
				}
				for(std::size_t i{current_size}; i < new_size; ++i) {
					binding->m_desc.m_Parameters.emplace_back(gsdk::FIELD_VARIANT);
				}
			}
		}

		current_binding = nullptr;
	}

	static detour<decltype(sq_setparamscheck)> sq_setparamscheck_detour;
	static SQRESULT sq_setparamscheck_detour_callback(HSQUIRRELVM v, SQInteger nparamscheck, const SQChar *typemask)
	{
		if(current_binding && (current_binding->m_flags & func_desc_t::SF_VA_FUNC)) {
			nparamscheck = -nparamscheck;
		}

		return sq_setparamscheck_detour(v, nparamscheck, typemask);
	}

	static void (gsdk::IServerGameDLL::*CreateNetworkStringTables_original)();
	void vmod::CreateNetworkStringTables_detour_callback(gsdk::IServerGameDLL *dll)
	{
		(dll->*CreateNetworkStringTables_original)();

		m_pDownloadableFileTable = sv_stringtables->FindTable(gsdk::DOWNLOADABLE_FILE_TABLENAME);
		m_pModelPrecacheTable = sv_stringtables->FindTable(gsdk::MODEL_PRECACHE_TABLENAME);
		m_pGenericPrecacheTable = sv_stringtables->FindTable(gsdk::GENERIC_PRECACHE_TABLENAME);
		m_pSoundPrecacheTable = sv_stringtables->FindTable(gsdk::SOUND_PRECACHE_TABLENAME);
		m_pDecalPrecacheTable = sv_stringtables->FindTable(gsdk::DECAL_PRECACHE_TABLENAME);

		g_pStringTableParticleEffectNames = sv_stringtables->FindTable("ParticleEffectNames");
		g_pStringTableEffectDispatch = sv_stringtables->FindTable("EffectDispatch");
		g_pStringTableVguiScreen = sv_stringtables->FindTable("VguiScreen");
		g_pStringTableMaterials = sv_stringtables->FindTable("Materials");
		g_pStringTableInfoPanel = sv_stringtables->FindTable("InfoPanel");
		g_pStringTableClientSideChoreoScenes = sv_stringtables->FindTable("Scenes");

		::vmod::vmod.recreate_script_stringtables();
	}

	static void (gsdk::IServerNetworkStringTableContainer::*RemoveAllTables_original)();
	void vmod::RemoveAllTables_detour_callback(gsdk::IServerNetworkStringTableContainer *cont)
	{
		::vmod::vmod.stringtables_removed();

		m_pDownloadableFileTable = nullptr;
		m_pModelPrecacheTable = nullptr;
		m_pGenericPrecacheTable = nullptr;
		m_pSoundPrecacheTable = nullptr;
		m_pDecalPrecacheTable = nullptr;

		g_pStringTableParticleEffectNames = nullptr;
		g_pStringTableEffectDispatch = nullptr;
		g_pStringTableVguiScreen = nullptr;
		g_pStringTableMaterials = nullptr;
		g_pStringTableInfoPanel = nullptr;
		g_pStringTableClientSideChoreoScenes = nullptr;

		(cont->*RemoveAllTables_original)();
	}

	static void (gsdk::IScriptVM::*SetErrorCallback_original)(gsdk::ScriptErrorFunc_t);
	static void SetErrorCallback_detour_callback(gsdk::IScriptVM *vm, gsdk::ScriptErrorFunc_t func)
	{
		if(in_vscript_server_init) {
			server_vs_error_cb = func;
			return;
		}

		(vm->*SetErrorCallback_original)(func);
	}

	static std::vector<const gsdk::ScriptFunctionBinding_t *> game_vscript_func_bindings;
	static std::vector<const gsdk::ScriptClassDesc_t *> game_vscript_class_bindings;

	static std::vector<const gsdk::ScriptFunctionBinding_t *> vmod_vscript_func_bindings;
	static std::vector<const gsdk::ScriptClassDesc_t *> vmod_vscript_class_bindings;

	static void (gsdk::IScriptVM::*RegisterFunction_original)(gsdk::ScriptFunctionBinding_t *);
	static void RegisterFunction_detour_callback(gsdk::IScriptVM *vm, gsdk::ScriptFunctionBinding_t *func)
	{
		(vm->*RegisterFunction_original)(func);
		std::vector<const gsdk::ScriptFunctionBinding_t *> &vec{vscript_server_init_called ? vmod_vscript_func_bindings : game_vscript_func_bindings};
		vec.emplace_back(func);
	}

	static bool (gsdk::IScriptVM::*RegisterClass_original)(gsdk::ScriptClassDesc_t *);
	static bool RegisterClass_detour_callback(gsdk::IScriptVM *vm, gsdk::ScriptClassDesc_t *desc)
	{
		bool ret{(vm->*RegisterClass_original)(desc)};
		std::vector<const gsdk::ScriptClassDesc_t *> &vec{vscript_server_init_called ? vmod_vscript_class_bindings : game_vscript_class_bindings};
		auto it{std::find(vec.begin(), vec.end(), desc)};
		if(it == vec.end()) {
			vec.emplace_back(desc);
		}
		return ret;
	}

	static gsdk::HSCRIPT (gsdk::IScriptVM::*RegisterInstance_original)(gsdk::ScriptClassDesc_t *, void *);
	static gsdk::HSCRIPT RegisterInstance_detour_callback(gsdk::IScriptVM *vm, gsdk::ScriptClassDesc_t *desc, void *ptr)
	{
		gsdk::HSCRIPT ret{(vm->*RegisterInstance_original)(desc, ptr)};
		std::vector<const gsdk::ScriptClassDesc_t *> &vec{vscript_server_init_called ? vmod_vscript_class_bindings : game_vscript_class_bindings};
		auto it{std::find(vec.begin(), vec.end(), desc)};
		if(it == vec.end()) {
			vec.emplace_back(desc);
		}
		return ret;
	}

	bool vmod::detours() noexcept
	{
		RegisterFunctionGuts_detour.initialize(RegisterFunctionGuts, RegisterFunctionGuts_detour_callback);
		RegisterFunctionGuts_detour.enable();

		sq_setparamscheck_detour.initialize(sq_setparamscheck, sq_setparamscheck_detour_callback);
		sq_setparamscheck_detour.enable();

		PrintFunc_detour.initialize(PrintFunc, PrintFunc_detour_callback);
		PrintFunc_detour.enable();

		ErrorFunc_detour.initialize(ErrorFunc, ErrorFunc_detour_callback);
		ErrorFunc_detour.enable();

		VScriptServerInit_detour.initialize(VScriptServerInit, VScriptServerInit_detour_callback);
		VScriptServerInit_detour.enable();

		VScriptServerTerm_detour.initialize(VScriptServerTerm, VScriptServerTerm_detour_callback);
		VScriptServerTerm_detour.enable();

		VScriptRunScript_detour.initialize(VScriptRunScript, VScriptRunScript_detour_callback);
		VScriptRunScript_detour.enable();

		CreateVM_original = swap_vfunc(vsmgr, &gsdk::IScriptManager::CreateVM, CreateVM_detour_callback);
		DestroyVM_original = swap_vfunc(vsmgr, &gsdk::IScriptManager::DestroyVM, DestroyVM_detour_callback);

		Run_original = swap_vfunc(vm_, static_cast<decltype(Run_original)>(&gsdk::IScriptVM::Run), Run_detour_callback);
		SetErrorCallback_original = swap_vfunc(vm_, &gsdk::IScriptVM::SetErrorCallback, SetErrorCallback_detour_callback);

		RegisterFunction_original = swap_vfunc(vm_, &gsdk::IScriptVM::RegisterFunction, RegisterFunction_detour_callback);
		RegisterClass_original = swap_vfunc(vm_, &gsdk::IScriptVM::RegisterClass, RegisterClass_detour_callback);
		RegisterInstance_original = swap_vfunc(vm_, &gsdk::IScriptVM::RegisterInstance_impl, RegisterInstance_detour_callback);

		CreateNetworkStringTables_original = swap_vfunc(gamedll, &gsdk::IServerGameDLL::CreateNetworkStringTables, CreateNetworkStringTables_detour_callback);

		RemoveAllTables_original = swap_vfunc(sv_stringtables, &gsdk::IServerNetworkStringTableContainer::RemoveAllTables, RemoveAllTables_detour_callback);

		return true;
	}

	bool vmod::load() noexcept
	{
		using namespace std::literals::string_literals;
		using namespace std::literals::string_view_literals;

		gsdk::ScriptLanguage_t script_language{gsdk::SL_SQUIRREL};

		switch(script_language) {
			case gsdk::SL_NONE: break;
			case gsdk::SL_GAMEMONKEY: scripts_extension = ".gm"sv; break;
			case gsdk::SL_SQUIRREL: scripts_extension = ".nut"sv; break;
			case gsdk::SL_LUA: scripts_extension = ".lua"sv; break;
			case gsdk::SL_PYTHON: scripts_extension = ".py"sv; break;
		}

		if(!symbol_cache::initialize()) {
			std::cout << "\033[0;31m"sv << "vmod: failed to initialize symbol cache\n"sv << "\033[0m"sv;
			return false;
		}

		std::filesystem::path exe_filename;

		{
			char exe[PATH_MAX];
			ssize_t len{readlink("/proc/self/exe", exe, sizeof(exe))};
			exe[len] = '\0';

			exe_filename = exe;
			exe_filename = exe_filename.filename();
			exe_filename.replace_extension();
		}

		std::string_view launcher_lib_name;
		if(exe_filename == "hl2_linux"sv) {
			launcher_lib_name = "bin/launcher.so"sv;
		} else if(exe_filename == "srcds_linux"sv) {
			launcher_lib_name = "bin/dedicated_srv.so"sv;
		} else {
			std::cout << "\033[0;31m"sv << "vmod: unsupported exe filename: '"sv << exe_filename << "'\n"sv << "\033[0m"sv;
			return false;
		}

		if(!launcher_lib.load(launcher_lib_name)) {
			std::cout << "\033[0;31m"sv << "vmod: failed to open launcher library: '"sv << launcher_lib.error_string() << "'\n"sv << "\033[0m"sv;
			return false;
		}

		old_spew = GetSpewOutputFunc();
		SpewOutputFunc(new_spew);

		std::string_view engine_lib_name{"bin/engine.so"sv};
		if(dedicated) {
			engine_lib_name = "bin/engine_srv.so"sv;
		}
		if(!engine_lib.load(engine_lib_name)) {
			error("vmod: failed to open engine library: '%s'\n", engine_lib.error_string().c_str());
			return false;
		}

		{
			char gamedir[PATH_MAX];
			sv_engine->GetGameDir(gamedir, sizeof(gamedir));

			game_dir_ = gamedir;
		}

		root_dir = game_dir_;
		root_dir /= "addons/vmod"sv;

		plugins_dir = root_dir;
		plugins_dir /= "plugins"sv;

		base_script_path = root_dir;
		base_script_path /= "base/vmod_base"sv;
		base_script_path.replace_extension(scripts_extension);

		std::filesystem::path server_lib_name{game_dir_};
		if(sv_engine->IsDedicatedServer()) {
			server_lib_name /= "bin/server_srv.so";
		} else {
			server_lib_name /= "bin/server.so";
		}
		if(!server_lib.load(server_lib_name)) {
			error("vmod: failed to open server library: '%s'\n"sv, server_lib.error_string().c_str());
			return false;
		}

		const auto &sv_symbols{server_lib.symbols()};
		const auto &sv_global_qual{sv_symbols.global()};

		auto g_Script_vscript_server_it{sv_global_qual.find("g_Script_vscript_server"s)};
		if(g_Script_vscript_server_it == sv_global_qual.end()) {
			error("vmod: missing 'g_Script_vscript_server' symbol\n"sv);
			return false;
		}

		auto g_pScriptVM_it{sv_global_qual.find("g_pScriptVM"s)};
		if(g_pScriptVM_it == sv_global_qual.end()) {
			error("vmod: missing 'g_pScriptVM' symbol\n"sv);
			return false;
		}

		auto VScriptServerInit_it{sv_global_qual.find("VScriptServerInit()"s)};
		if(VScriptServerInit_it == sv_global_qual.end()) {
			error("vmod: missing 'VScriptServerInit' symbol\n"sv);
			return false;
		}

		auto VScriptServerTerm_it{sv_global_qual.find("VScriptServerTerm()"s)};
		if(VScriptServerTerm_it == sv_global_qual.end()) {
			error("vmod: missing 'VScriptServerTerm' symbol\n"sv);
			return false;
		}

		auto VScriptRunScript_it{sv_global_qual.find("VScriptRunScript(char const*, HSCRIPT__*, bool)"s)};
		if(VScriptRunScript_it == sv_global_qual.end()) {
			error("vmod: missing 'VScriptRunScript' symbol\n"sv);
			return false;
		}

		auto CTFGameRules_it{sv_symbols.find("CTFGameRules"s)};
		if(CTFGameRules_it == sv_symbols.end()) {
			error("vmod: missing 'CTFGameRules' symbols\n"sv);
			return false;
		}

		auto RegisterScriptFunctions_it{CTFGameRules_it->second.find("RegisterScriptFunctions()"s)};
		if(RegisterScriptFunctions_it == CTFGameRules_it->second.end()) {
			error("vmod: missing 'CTFGameRules::RegisterScriptFunctions()' symbol\n"sv);
			return false;
		}

		auto sv_ScriptClassDesc_t_it{sv_symbols.find("ScriptClassDesc_t"s)};
		if(sv_ScriptClassDesc_t_it == sv_symbols.end()) {
			error("vmod: missing 'ScriptClassDesc_t' symbol\n"sv);
			return false;
		}

		auto sv_GetDescList_it{sv_ScriptClassDesc_t_it->second.find("GetDescList()"s)};
		if(sv_GetDescList_it == sv_ScriptClassDesc_t_it->second.end()) {
			error("vmod: missing 'ScriptClassDesc_t::GetDescList()' symbol\n"sv);
			return false;
		}

		auto sv_pHead_it{sv_GetDescList_it->second.find("pHead"s)};
		if(sv_pHead_it == sv_GetDescList_it->second.end()) {
			error("vmod: missing 'ScriptClassDesc_t::GetDescList()::pHead' symbol\n"sv);
			return false;
		}

		std::string_view vstdlib_lib_name{"bin/libvstdlib.so"sv};
		if(sv_engine->IsDedicatedServer()) {
			vstdlib_lib_name = "bin/libvstdlib_srv.so"sv;
		}
		if(!vstdlib_lib.load(vstdlib_lib_name)) {
			error("vmod: failed to open vstdlib library: %s\n"sv, vstdlib_lib.error_string().c_str());
			return false;
		}

		std::string_view vscript_lib_name{"bin/vscript.so"sv};
		if(sv_engine->IsDedicatedServer()) {
			vscript_lib_name = "bin/vscript_srv.so"sv;
		}
		if(!vscript_lib.load(vscript_lib_name)) {
			error("vmod: failed to open vscript library: '%s'\n"sv, vscript_lib.error_string().c_str());
			return false;
		}

		const auto &vscript_symbols{vscript_lib.symbols()};
		const auto &vscript_global_qual{vscript_symbols.global()};

		auto sq_getversion_it{vscript_global_qual.find("sq_getversion"s)};
		if(sq_getversion_it == vscript_global_qual.end()) {
			error("vmod: missing 'sq_getversion' symbol\n"sv);
			return false;
		}

		vm_ = vsmgr->CreateVM(script_language);
		if(!vm_) {
			error("vmod: failed to create VM\n"sv);
			return false;
		}

		{
			SQInteger game_sq_ver{sq_getversion_it->second.func<decltype(::sq_getversion)>()()};
			SQInteger curr_sq_ver{::sq_getversion()};

			if(curr_sq_ver != SQUIRREL_VERSION_NUMBER) {
				error("vmod: mismatched squirrel header '%i' vs '%i'\n"sv, curr_sq_ver, SQUIRREL_VERSION_NUMBER);
				return false;
			}

			if(game_sq_ver != curr_sq_ver) {
				error("vmod: mismatched squirrel versions '%i' vs '%i'\n"sv, game_sq_ver, curr_sq_ver);
				return false;
			}

			script_variant_t game_sq_versionnumber;
			if(!vm_->GetValue(nullptr, "_versionnumber_", &game_sq_versionnumber)) {
				error("vmod: failed to get _versionnumber_ value\n"sv);
				return false;
			}

			script_variant_t game_sq_version;
			if(!vm_->GetValue(nullptr, "_version_", &game_sq_version)) {
				error("vmod: failed to get _version_ value\n"sv);
				return false;
			}

			info("vmod: squirrel info:\n");
			info("vmod:   vmod:\n");
			info("vmod:    SQUIRREL_VERSION: %s\n", SQUIRREL_VERSION);
			info("vmod:    SQUIRREL_VERSION_NUMBER: %i\n", SQUIRREL_VERSION_NUMBER);
			info("vmod:    sq_getversion: %i\n", curr_sq_ver);
			info("vmod:   game:\n");
			info("vmod:    _version_: %s\n", game_sq_version.m_pszString);
			info("vmod:    _versionnumber_: %i\n", game_sq_versionnumber.m_int);
			info("vmod:    sq_getversion: %i\n", game_sq_ver);
		}

		auto g_Script_init_it{vscript_global_qual.find("g_Script_init"s)};

		auto sq_setparamscheck_it{vscript_global_qual.find("sq_setparamscheck"s)};
		if(sq_setparamscheck_it == vscript_global_qual.end()) {
			error("vmod: missing 'sq_setparamscheck' symbol\n"sv);
			return false;
		}

		auto CSquirrelVM_it{vscript_symbols.find("CSquirrelVM"s)};
		if(CSquirrelVM_it == vscript_symbols.end()) {
			error("vmod: missing 'CSquirrelVM' symbols\n"sv);
			return false;
		}

		auto CreateArray_it{CSquirrelVM_it->second.find("CreateArray(CVariantBase<CVariantDefaultAllocator>&)"s)};
		if(CreateArray_it == CSquirrelVM_it->second.end()) {
			error("vmod: missing 'CSquirrelVM::CreateArray(CVariantBase<CVariantDefaultAllocator>&)' symbol\n"sv);
			return false;
		}

		auto GetArrayCount_it{CSquirrelVM_it->second.find("GetArrayCount(HSCRIPT__*)"s)};
		if(GetArrayCount_it == CSquirrelVM_it->second.end()) {
			error("vmod: missing 'CSquirrelVM::GetArrayCount(HSCRIPT__*)' symbol\n"sv);
			return false;
		}

		auto PrintFunc_it{CSquirrelVM_it->second.find("PrintFunc(SQVM*, char const*, ...)"s)};
		if(PrintFunc_it == CSquirrelVM_it->second.end()) {
			error("vmod: missing 'CSquirrelVM::PrintFunc(SQVM*, char const*, ...)' symbol\n"sv);
			return false;
		}

		auto ErrorFunc_it{CSquirrelVM_it->second.find("ErrorFunc(SQVM*, char const*, ...)"s)};
		if(ErrorFunc_it == CSquirrelVM_it->second.end()) {
			error("vmod: missing 'CSquirrelVM::ErrorFunc(SQVM*, char const*, ...)' symbol\n"sv);
			return false;
		}

		auto RegisterFunctionGuts_it{CSquirrelVM_it->second.find("RegisterFunctionGuts(ScriptFunctionBinding_t*, ScriptClassDesc_t*)"s)};
		if(RegisterFunctionGuts_it == CSquirrelVM_it->second.end()) {
			error("vmod: missing 'CSquirrelVM::RegisterFunctionGuts(ScriptFunctionBinding_t*, ScriptClassDesc_t*)' symbol\n"sv);
			return false;
		}

		if(g_Script_init_it != vscript_global_qual.end()) {
			g_Script_init = g_Script_init_it->second.addr<const unsigned char *>();
		}

		RegisterScriptFunctions = RegisterScriptFunctions_it->second.mfp<decltype(RegisterScriptFunctions)>();
		VScriptServerInit = VScriptServerInit_it->second.func<decltype(VScriptServerInit)>();
		VScriptServerTerm = VScriptServerTerm_it->second.func<decltype(VScriptServerTerm)>();
		VScriptRunScript = VScriptRunScript_it->second.func<decltype(VScriptRunScript)>();
		g_Script_vscript_server = g_Script_vscript_server_it->second.addr<const unsigned char *>();
		g_pScriptVM = g_pScriptVM_it->second.addr<gsdk::IScriptVM **>();

		CreateArray = CreateArray_it->second.mfp<decltype(CreateArray)>();
		GetArrayCount = GetArrayCount_it->second.mfp<decltype(GetArrayCount)>();

		PrintFunc = PrintFunc_it->second.func<decltype(PrintFunc)>();
		ErrorFunc = ErrorFunc_it->second.func<decltype(ErrorFunc)>();
		RegisterFunctionGuts = RegisterFunctionGuts_it->second.mfp<decltype(RegisterFunctionGuts)>();
		sq_setparamscheck = sq_setparamscheck_it->second.func<decltype(sq_setparamscheck)>();

		sv_classdesc_pHead = sv_pHead_it->second.addr<gsdk::ScriptClassDesc_t **>();

		vm_->SetOutputCallback(vscript_output);
		vm_->SetErrorCallback(vscript_error_output);

		if(g_Script_init) {
			write_file(root_dir/"internal_scripts"sv/"init.nut"sv, g_Script_init, std::strlen(reinterpret_cast<const char *>(g_Script_init)+1));
		}

		write_file(root_dir/"internal_scripts"sv/"vscript_server.nut"sv, g_Script_vscript_server, std::strlen(reinterpret_cast<const char *>(g_Script_vscript_server)+1));

		if(!detours()) {
			return false;
		}

		if(!binding_mods()) {
			return false;
		}

		scope_ = vm_->CreateScope("vmod", nullptr);
		if(!scope_ || scope_ == gsdk::INVALID_HSCRIPT) {
			error("vmod: failed to create vmod scope\n"sv);
			return false;
		}

		cvar_dll_id_ = cvar->AllocateDLLIdentifier();

		vmod_reload_plugins.initialize("vmod_reload_plugins"sv, [this](const gsdk::CCommand &) noexcept -> void {
			for(const auto &pl : plugins) {
				pl->reload();
			}

			if(plugins_loaded) {
				for(const auto &pl : plugins) {
					if(!*pl) {
						continue;
					}

					pl->all_plugins_loaded();
				}
			}
		});

		vmod_unload_plugins.initialize("vmod_unload_plugins"sv, [this](const gsdk::CCommand &) noexcept -> void {
			for(const auto &pl : plugins) {
				pl->unload();
			}

			plugins.clear();

			plugins_loaded = false;
		});

		vmod_unload_plugin.initialize("vmod_unload_plugin"sv, [this](const gsdk::CCommand &args) noexcept -> void {
			if(args.m_nArgc != 2) {
				error("vmod: usage: vmod_unload_plugin <path>\n");
				return;
			}

			std::filesystem::path path{args.m_ppArgv[1]};
			if(!path.is_absolute()) {
				path = (plugins_dir/path);
			}
			path.replace_extension(scripts_extension);

			for(auto it{plugins.begin()}; it != plugins.end(); ++it) {
				const std::filesystem::path &pl_path{static_cast<std::filesystem::path>(*(*it))};

				if(pl_path == path) {
					plugins.erase(it);
					error("vmod: unloaded plugin '%s'\n", path.c_str());
					return;
				}
			}

			error("vmod: plugin '%s' not found\n", path.c_str());
		});

		vmod_load_plugin.initialize("vmod_load_plugin"sv, [this](const gsdk::CCommand &args) noexcept -> void {
			if(args.m_nArgc != 2) {
				error("vmod: usage: vmod_load_plugin <path>\n");
				return;
			}

			std::filesystem::path path{args.m_ppArgv[1]};
			if(!path.is_absolute()) {
				path = (plugins_dir/path);
			}
			path.replace_extension(scripts_extension);

			for(const auto &pl : plugins) {
				const std::filesystem::path &pl_path{static_cast<std::filesystem::path>(*pl)};

				if(pl_path == path) {
					if(pl->reload()) {
						success("vmod: plugin '%s' reloaded\n", path.c_str());
						if(plugins_loaded) {
							pl->all_plugins_loaded();
						}
					}
					return;
				}
			}

			plugin &pl{*plugins.emplace_back(new plugin{std::move(path)})};
			if(pl.load()) {
				success("vmod: plugin '%s' loaded\n", static_cast<std::filesystem::path>(pl).c_str());
				if(plugins_loaded) {
					pl.all_plugins_loaded();
				}
			}
		});

		vmod_list_plugins.initialize("vmod_list_plugins"sv, [this](const gsdk::CCommand &args) noexcept -> void {
			if(args.m_nArgc != 1) {
				error("vmod: usage: vmod_list_plugins\n");
				return;
			}

			if(plugins.empty()) {
				info("vmod: no plugins loaded\n");
				return;
			}

			for(const auto &pl : plugins) {
				if(*pl) {
					success("'%s'\n", static_cast<std::filesystem::path>(*pl).c_str());
				} else {
					error("'%s'\n", static_cast<std::filesystem::path>(*pl).c_str());
				}
			}
		});

		vmod_refresh_plugins.initialize("vmod_refresh_plugins"sv, [this](const gsdk::CCommand &) noexcept -> void {
			plugins.clear();

			for(const auto &file : std::filesystem::directory_iterator{plugins_dir}) {
				if(!file.is_regular_file()) {
					continue;
				}

				std::filesystem::path path{file.path()};
				if(path.extension() != scripts_extension) {
					continue;
				}

				plugin *pl{new plugin{std::move(path)}};
				pl->load();

				plugins.emplace_back(pl);
			}

			for(const auto &pl : plugins) {
				if(!*pl) {
					continue;
				}

				pl->all_plugins_loaded();
			}

			plugins_loaded = true;
		});

		if(!VScriptServerInit_detour_callback()) {
			error("vmod: VScriptServerInit failed\n"sv);
			return false;
		}

		(reinterpret_cast<gsdk::CTFGameRules *>(0xbebebebe)->*RegisterScriptFunctions)();

		vscript_server_init_called = true;

		if(vm_->GetLanguage() == gsdk::SL_SQUIRREL) {
			server_init_script = vm_->CompileScript(reinterpret_cast<const char *>(g_Script_vscript_server), "vscript_server.nut");
			if(!server_init_script || server_init_script == gsdk::INVALID_HSCRIPT) {
				error("vmod: failed to compile server init script\n"sv);
				return false;
			}
		} else {
			error("vmod: server init script not supported on this language\n"sv);
			return false;
		}

		if(vm_->Run(server_init_script, nullptr, true) == gsdk::SCRIPT_ERROR) {
			error("vmod: failed to run server init script\n"sv);
			return false;
		}

		std::string base_script_name{"vmod_base"sv};
		base_script_name += scripts_extension;

		if(std::filesystem::exists(base_script_path)) {
			{
				std::unique_ptr<unsigned char[]> script_data{read_file(base_script_path)};

				base_script = vm_->CompileScript(reinterpret_cast<const char *>(script_data.get()), base_script_path.c_str());
				if(!base_script || base_script == gsdk::INVALID_HSCRIPT) {
				#ifndef __VMOD_BASE_SCRIPT_HEADER_INCLUDED
					error("vmod: failed to compile base script '%s'\n"sv, base_script_path.c_str());
					return false;
				#else
					base_script = vm_->CompileScript(reinterpret_cast<const char *>(__vmod_base_script), base_script_name.c_str());
					if(!base_script || base_script == gsdk::INVALID_HSCRIPT) {
						error("vmod: failed to compile base script\n"sv);
						return false;
					}
				#endif
				} else {
					base_script_from_file = true;
				}
			}
		} else {
		#ifndef __VMOD_BASE_SCRIPT_HEADER_INCLUDED
			error("vmod: missing base script '%s'\n"sv, base_script_path.c_str());
			return false;
		#else
			base_script = vm_->CompileScript(reinterpret_cast<const char *>(__vmod_base_script), base_script_name.c_str());
			if(!base_script || base_script == gsdk::INVALID_HSCRIPT) {
				error("vmod: failed to compile base script\n"sv);
				return false;
			}
		#endif
		}

		base_script_scope = vm_->CreateScope("__vmod_base_script_scope__", nullptr);
		if(!base_script_scope || base_script_scope == gsdk::INVALID_HSCRIPT) {
			error("vmod: failed to create base script scope\n"sv);
			return false;
		}

		if(vm_->Run(base_script, base_script_scope, true) == gsdk::SCRIPT_ERROR) {
			if(base_script_from_file) {
				error("vmod: failed to run base script '%s'\n"sv, base_script_path.c_str());
			} else {
				error("vmod: failed to run base script\n"sv);
			}
			return false;
		}

		auto get_func_from_base_script{[this](gsdk::HSCRIPT &func, std::string_view name) noexcept -> bool {
			func = vm_->LookupFunction(name.data(), base_script_scope);
			if(!func || func == gsdk::INVALID_HSCRIPT) {
				if(base_script_from_file) {
					error("vmod: base script '%s' missing '%s' function\n"sv, base_script_path.c_str(), name.data());
				} else {
					error("vmod: base script missing '%s' function\n"sv, name.data());
				}
				return false;
			}
			return true;
		}};

		if(!get_func_from_base_script(to_string_func, "__to_string__"sv)) {
			return false;
		}

		if(!get_func_from_base_script(to_int_func, "__to_int__"sv)) {
			return false;
		}

		if(!get_func_from_base_script(to_float_func, "__to_float__"sv)) {
			return false;
		}

		if(!get_func_from_base_script(to_bool_func, "__to_bool__"sv)) {
			return false;
		}

		if(!get_func_from_base_script(typeof_func, "__typeof__"sv)) {
			return false;
		}

		if(!get_func_from_base_script(funcisg_func, "__get_func_sig__"sv)) {
			return false;
		}

		return true;
	}

	static script_variant_t call_to_func(gsdk::HSCRIPT func, gsdk::HSCRIPT value) noexcept
	{
		script_variant_t ret;
		script_variant_t arg{value};

		if(vmod.vm()->ExecuteFunction(func, &arg, 1, &ret, nullptr, true) == gsdk::SCRIPT_ERROR) {
			null_variant(ret);
			return ret;
		}

		return ret;
	}

	std::string_view vmod::to_string(gsdk::HSCRIPT value) const noexcept
	{
		script_variant_t ret{call_to_func(to_string_func, value)};

		if(ret.m_type != gsdk::FIELD_CSTRING) {
			return {};
		}

		static std::string temp_buffer;

		temp_buffer = ret.m_pszString;

		return temp_buffer;
	}

	int vmod::to_int(gsdk::HSCRIPT value) const noexcept
	{
		script_variant_t ret{call_to_func(to_int_func, value)};

		if(ret.m_type != gsdk::FIELD_INTEGER) {
			return 0;
		}

		return ret.m_int;
	}

	float vmod::to_float(gsdk::HSCRIPT value) const noexcept
	{
		script_variant_t ret{call_to_func(to_float_func, value)};

		if(ret.m_type != gsdk::FIELD_FLOAT) {
			return 0.0f;
		}

		return ret.m_float;
	}

	bool vmod::to_bool(gsdk::HSCRIPT value) const noexcept
	{
		script_variant_t ret{call_to_func(to_bool_func, value)};

		if(ret.m_type != gsdk::FIELD_BOOLEAN) {
			return false;
		}

		return ret.m_bool;
	}

	std::string_view vmod::typeof_(gsdk::HSCRIPT value) const noexcept
	{
		script_variant_t ret{call_to_func(typeof_func, value)};

		if(ret.m_type != gsdk::FIELD_CSTRING) {
			return {};
		}

		return ret.m_pszString;
	}

	bool __vmod_to_bool(gsdk::HSCRIPT object) noexcept
	{ return vmod.to_bool(object); }
	float __vmod_to_float(gsdk::HSCRIPT object) noexcept
	{ return vmod.to_float(object); }
	int __vmod_to_int(gsdk::HSCRIPT object) noexcept
	{ return vmod.to_int(object); }
	std::string_view __vmod_to_string(gsdk::HSCRIPT object) noexcept
	{ return vmod.to_string(object); }
	std::string_view __vmod_typeof(gsdk::HSCRIPT object) noexcept
	{ return vmod.typeof_(object); }

	static inline void ident(std::string &file, std::size_t num) noexcept
	{ file.insert(file.end(), num, '\t'); }

	static std::string_view datatype_to_str(gsdk::ScriptDataType_t type) noexcept
	{
		using namespace std::literals::string_view_literals;

		switch(type) {
			case gsdk::FIELD_VOID:
			return "void"sv;
			case gsdk::FIELD_CHARACTER:
			return "char"sv;
			case gsdk::FIELD_SHORT:
			case gsdk::FIELD_POSITIVEINTEGER_OR_NULL:
			case gsdk::FIELD_INTEGER:
			case gsdk::FIELD_UINT:
			case gsdk::FIELD_INTEGER64:
			case gsdk::FIELD_UINT64:
			return "int"sv;
			case gsdk::FIELD_DOUBLE:
			case gsdk::FIELD_FLOAT:
			return "float"sv;
			case gsdk::FIELD_BOOLEAN:
			return "bool"sv;
			case gsdk::FIELD_HSCRIPT_NEW_INSTANCE:
			case gsdk::FIELD_HSCRIPT:
			return "handle"sv;
			case gsdk::FIELD_VECTOR:
			return "Vector"sv;
			case gsdk::FIELD_QANGLE:
			return "QAngle"sv;
			case gsdk::FIELD_QUATERNION:
			return "Quaternion"sv;
			case gsdk::FIELD_STRING:
			case gsdk::FIELD_CSTRING:
			return "string"sv;
			case gsdk::FIELD_VARIANT:
			return "variant"sv;
			default:
			return "<<unknown>>"sv;
		}
	}

	static std::string_view get_func_desc_desc(const gsdk::ScriptFuncDescriptor_t *desc) noexcept
	{
		if(desc->m_pszDescription[0] == '#') {
			const char *ptr{desc->m_pszDescription};
			while(*ptr != ':') {
				++ptr;
			}
			++ptr;
			return ptr;
		} else {
			return desc->m_pszDescription;
		}
	}

	bool vmod::write_func(const gsdk::ScriptFunctionBinding_t *func, bool global, std::size_t depth, std::string &file, bool respect_hide) const noexcept
	{
		using namespace std::literals::string_view_literals;

		const gsdk::ScriptFuncDescriptor_t &func_desc{func->m_desc};

		if(respect_hide) {
			if(func_desc.m_pszDescription && func_desc.m_pszDescription[0] == '@') {
				return false;
			}
		}

		if(func_desc.m_pszDescription && func_desc.m_pszDescription[0] != '\0' && func_desc.m_pszDescription[0] != '@') {
			ident(file, depth);
			file += "//"sv;
			file += get_func_desc_desc(&func_desc);
			file += '\n';
		}

		ident(file, depth);

		if(global) {
			if(!(func->m_flags & gsdk::SF_MEMBER_FUNC)) {
				file += "static "sv;
			}
		}

		file += datatype_to_str(func_desc.m_ReturnType);
		file += ' ';
		file += func_desc.m_pszScriptName;

		file += '(';
		std::size_t num_args{func_desc.m_Parameters.size()};
		for(std::size_t j{0}; j < num_args; ++j) {
			file += datatype_to_str(func_desc.m_Parameters[j]);
			file += ", "sv;
		}
		if(func->m_flags & func_desc_t::SF_VA_FUNC) {
			file += "..."sv;
		} else {
			if(num_args > 0) {
				file.erase(file.end()-2, file.end());
			}
		}
		file += ");\n\n"sv;

		return true;
	}

	static std::string_view get_class_desc_name(const gsdk::ScriptClassDesc_t *desc) noexcept
	{
		if(desc->m_pNextDesc == reinterpret_cast<const gsdk::ScriptClassDesc_t *>(0xbebebebe)) {
			const extra_class_desc_t &extra{reinterpret_cast<const base_class_desc_t<empty_class> *>(desc)->extra()};
			if(!extra.doc_class_name.empty()) {
				return extra.doc_class_name;
			}
		}

		return desc->m_pszScriptName;
	}

	static std::string_view get_class_desc_desc(const gsdk::ScriptClassDesc_t *desc) noexcept
	{
		if(desc->m_pszDescription[0] == '!') {
			return desc->m_pszDescription + 1;
		} else {
			return desc->m_pszDescription;
		}
	}

	bool vmod::write_class(const gsdk::ScriptClassDesc_t *desc, bool global, std::size_t depth, std::string &file, bool respect_hide) const noexcept
	{
		using namespace std::literals::string_view_literals;

		if(respect_hide) {
			if(desc->m_pszDescription && desc->m_pszDescription[0] == '@') {
				return false;
			}
		}

		if(global) {
			if(desc->m_pszDescription && desc->m_pszDescription[0] != '\0' && desc->m_pszDescription[0] != '@') {
				ident(file, depth);
				file += "//"sv;
				file += get_class_desc_desc(desc);
				file += '\n';
			}

			ident(file, depth);
			file += "class "sv;
			file += get_class_desc_name(desc);

			if(desc->m_pBaseDesc) {
				file += " : "sv;
				file += get_class_desc_name(desc->m_pBaseDesc);
			}

			file += '\n';
			ident(file, depth);
			file += "{\n"sv;
		}

		std::size_t written{0};
		for(std::size_t i{0}; i < desc->m_FunctionBindings.size(); ++i) {
			if(write_func(&desc->m_FunctionBindings[i], global, global ? depth+1 : depth, file, respect_hide)) {
				++written;
			}
		}
		if(written > 0) {
			file.erase(file.end()-1, file.end());
		}

		if(global) {
			ident(file, depth);
			file += "};"sv;
		}

		return true;
	}

	void vmod::write_docs(const std::filesystem::path &dir, const std::vector<const gsdk::ScriptClassDesc_t *> &vec, bool respect_hide) const noexcept
	{
		using namespace std::literals::string_view_literals;

		for(const gsdk::ScriptClassDesc_t *desc : vec) {
			std::string file;
			if(!write_class(desc, true, 0, file, respect_hide)) {
				continue;
			}

			std::filesystem::path doc_path{dir};
			doc_path /= get_class_desc_name(desc);
			doc_path.replace_extension(".txt"sv);

			write_file(doc_path, reinterpret_cast<const unsigned char *>(file.c_str()), file.length());
		}
	}

	void vmod::write_docs(const std::filesystem::path &dir, const std::vector<const gsdk::ScriptFunctionBinding_t *> &vec, bool respect_hide) const noexcept
	{
		using namespace std::literals::string_view_literals;

		std::string file;

		std::size_t written{0};
		for(const gsdk::ScriptFunctionBinding_t *desc : vec) {
			if(write_func(desc, false, 0, file, respect_hide)) {
				++written;
			}
		}
		if(written > 0) {
			file.erase(file.end()-1, file.end());
		}

		std::filesystem::path doc_path{dir};
		doc_path /= "globals"sv;
		doc_path.replace_extension(".txt"sv);

		write_file(doc_path, reinterpret_cast<const unsigned char *>(file.c_str()), file.length());
	}

	void vmod::write_syms_docs(const std::filesystem::path &dir) const noexcept
	{
		using namespace std::literals::string_view_literals;

		std::string file;

		file += "namespace syms\n{\n"sv;

		write_class(&lib_symbols_desc, true, 1, file, false);
		file += "\n\n"sv;

		write_class(&lib_symbols_singleton::qual_it_desc, true, 1, file, false);
		file += "\n\n"sv;

		write_class(&lib_symbols_singleton::name_it_desc, true, 1, file, false);
		file += "\n\n"sv;

		ident(file, 1);
		file += get_class_desc_name(&lib_symbols_desc);
		file += " sv;"sv;

		file += "\n}"sv;

		std::filesystem::path doc_path{dir};
		doc_path /= "syms"sv;
		doc_path.replace_extension(".txt"sv);

		write_file(doc_path, reinterpret_cast<const unsigned char *>(file.c_str()), file.length());
	}

	void vmod::write_strtables_docs(const std::filesystem::path &dir) const noexcept
	{
		using namespace std::literals::string_view_literals;

		std::string file;

		file += "namespace strtables\n{\n"sv;

		write_class(&stringtable_desc, true, 1, file, false);
		file += "\n\n"sv;

		for(const auto &it : script_stringtables) {
			ident(file, 1);
			file += get_class_desc_name(&stringtable_desc);
			file += ' ';
			file += it.first;
			file += ";\n"sv;
		}

		file += '}';

		std::filesystem::path doc_path{dir};
		doc_path /= "strtables"sv;
		doc_path.replace_extension(".txt"sv);

		write_file(doc_path, reinterpret_cast<const unsigned char *>(file.c_str()), file.length());
	}

	void vmod::write_yaml_docs(const std::filesystem::path &dir) const noexcept
	{
		using namespace std::literals::string_view_literals;

		std::string file;

		file += "namespace yaml\n{\n"sv;

		write_class(&yaml_desc, true, 1, file, false);
		file += "\n\n"sv;

		write_class(&yaml_singleton_desc, false, 1, file, false);

		file += '}';

		std::filesystem::path doc_path{dir};
		doc_path /= "yaml"sv;
		doc_path.replace_extension(".txt"sv);

		write_file(doc_path, reinterpret_cast<const unsigned char *>(file.c_str()), file.length());
	}

	void vmod::write_fs_docs(const std::filesystem::path &dir) const noexcept
	{
		using namespace std::literals::string_view_literals;

		std::string file;

		file += "namespace fs\n{\n"sv;

		write_class(&filesystem_singleton_desc, false, 1, file, false);
		file += '\n';

		ident(file, 1);
		file += "string game_dir;\n"sv;

		file += '}';

		std::filesystem::path doc_path{dir};
		doc_path /= "fs"sv;
		doc_path.replace_extension(".txt"sv);

		write_file(doc_path, reinterpret_cast<const unsigned char *>(file.c_str()), file.length());
	}

	void vmod::write_cvar_docs(const std::filesystem::path &dir) const noexcept
	{
		using namespace std::literals::string_view_literals;

		std::string file;

		file += "namespace cvar\n{\n"sv;

		write_class(&cvar_singleton::script_cvar_desc, true, 1, file, false);
		file += "\n\n"sv;

		ident(file, 1);
		file += "enum class flags\n"sv;
		ident(file, 1);
		file += "{\n"sv;
		write_enum_table(file, 2, ::vmod::cvar_singleton.flags_table, write_enum_how::flags);
		ident(file, 1);
		file += "};\n\n"sv;

		write_class(&cvar_singleton_desc, false, 1, file, false);

		file += '}';

		std::filesystem::path doc_path{dir};
		doc_path /= "cvar"sv;
		doc_path.replace_extension(".txt"sv);

		write_file(doc_path, reinterpret_cast<const unsigned char *>(file.c_str()), file.length());
	}

	void vmod::write_mem_docs(const std::filesystem::path &dir) const noexcept
	{
		using namespace std::literals::string_view_literals;

		std::string file;

		file += "namespace mem\n{\n"sv;

		write_class(&mem_block_desc, true, 1, file, false);
		file += "\n\n"sv;

		write_class(&memory_desc, false, 1, file, false);

		file += '\n';

		ident(file, 1);
		file += "namespace types\n"sv;
		ident(file, 1);
		file += "{\n"sv;
		ident(file, 2);
		file += "struct type\n"sv;
		ident(file, 2);
		file += "{\n"sv;
		ident(file, 3);
		file += "int size;\n"sv;
		ident(file, 3);
		file += "int alignment;\n"sv;
		ident(file, 3);
		file += "int id;\n"sv;
		ident(file, 3);
		file += "string name;\n"sv;
		ident(file, 2);
		file += "};\n\n"sv;
		for(const memory_singleton::mem_type &type : memory_singleton.types) {
			ident(file, 2);
			file += "type "sv;
			file += type.name;
			file += ";\n"sv;
		}
		ident(file, 1);
		file += "}\n}"sv;

		std::filesystem::path doc_path{dir};
		doc_path /= "mem"sv;
		doc_path.replace_extension(".txt"sv);

		write_file(doc_path, reinterpret_cast<const unsigned char *>(file.c_str()), file.length());
	}

	void vmod::write_ffi_docs(const std::filesystem::path &dir) const noexcept
	{
		using namespace std::literals::string_view_literals;

		std::string file;

		file += "namespace ffi\n{\n"sv;

		write_class(&detour_desc, true, 1, file, false);
		file += "\n\n"sv;

		write_class(&cif_desc, true, 1, file, false);
		file += "\n\n"sv;

		write_class(&ffi_singleton_desc, false, 1, file, false);

		file += '\n';

		ident(file, 1);
		file += "enum class types\n"sv;
		ident(file, 1);
		file += "{\n"sv;
		write_enum_table(file, 2, ::vmod::ffi_singleton.types_table, write_enum_how::name);
		ident(file, 1);
		file += "};\n\n"sv;

		ident(file, 1);
		file += "enum class abi\n"sv;
		ident(file, 1);
		file += "{\n"sv;
		write_enum_table(file, 2, ::vmod::ffi_singleton.abi_table, write_enum_how::normal);
		ident(file, 1);
		file += "};\n}"sv;

		std::filesystem::path doc_path{dir};
		doc_path /= "ffi"sv;
		doc_path.replace_extension(".txt"sv);

		write_file(doc_path, reinterpret_cast<const unsigned char *>(file.c_str()), file.length());
	}

	void vmod::write_vmod_docs(const std::filesystem::path &dir) const noexcept
	{
		using namespace std::literals::string_view_literals;

		std::string file;

		file += "namespace vmod\n{\n"sv;
		write_class(&vmod_desc, false, 1, file, false);
		file += '\n';

		ident(file, 1);
		file += "string root_dir;\n\n"sv;

		ident(file, 1);
		file += "namespace plugins\n"sv;
		ident(file, 1);
		file += "{\n"sv;
		ident(file, 1);
		file += "}\n\n"sv;

		ident(file, 1);
		file += "namespace syms;\n\n"sv;
		write_syms_docs(dir);

		ident(file, 1);
		file += "namespace yaml;\n\n"sv;
		write_yaml_docs(dir);

		ident(file, 1);
		file += "namespace ffi;\n\n"sv;
		write_ffi_docs(dir);

		ident(file, 1);
		file += "namespace fs;\n\n"sv;
		write_fs_docs(dir);

		ident(file, 1);
		file += "namespace cvar;\n\n"sv;
		write_cvar_docs(dir);

		ident(file, 1);
		file += "namespace mem;\n\n"sv;
		write_mem_docs(dir);

		ident(file, 1);
		file += "namespace strtables;\n"sv;
		write_strtables_docs(dir);

		file += '}';

		std::filesystem::path doc_path{dir};
		doc_path /= "vmod"sv;
		doc_path.replace_extension(".txt"sv);

		write_file(doc_path, reinterpret_cast<const unsigned char *>(file.c_str()), file.length());
	}

	void vmod::write_enum_table(std::string &file, std::size_t depth, gsdk::HSCRIPT enum_table, write_enum_how how) const noexcept
	{
		using namespace std::literals::string_view_literals;

		std::unordered_map<int, std::string> bit_str_map;

		int num2{vm_->GetNumTableEntries(enum_table)};
		for(int j{0}, it2{0}; j < num2 && it2 != -1; ++j) {
			script_variant_t key2;
			script_variant_t value2;
			it2 = vm_->GetKeyValue(enum_table, it2, &key2, &value2);

			std::string_view value_name{key2.get<std::string_view>()};

			ident(file, depth);
			file += value_name;
			if(how != write_enum_how::name) {
				file += " = "sv;
			}

			if(how == write_enum_how::flags) {
				unsigned int val{value2.get<unsigned int>()};

				std::vector<int> bits;

				for(int k{0}; val; val >>= 1, ++k) {
					if(val & 1) {
						bits.emplace_back(k);
					}
				}

				std::size_t num_bits{bits.size()};

				if(num_bits > 0) {
					std::string temp_bit_str;

					if(num_bits > 1) {
						file += '(';
					}
					for(int bit : bits) {
						auto name_it{bit_str_map.find(bit)};
						if(name_it != bit_str_map.end()) {
							file += name_it->second;
							file += '|';
						} else {
							file += "(1 << "sv;

							temp_bit_str.resize(6);

							char *begin{temp_bit_str.data()};
							char *end{temp_bit_str.data() + 6};

							std::to_chars_result tc_res{std::to_chars(begin, end, bit)};
							tc_res.ptr[0] = '\0';

							file += begin;
							file += ")|"sv;
						}
					}
					file.pop_back();
					if(num_bits > 1) {
						file += ')';
					}

					if(num_bits == 1) {
						bit_str_map.emplace(bits[0], value_name);
					}
				} else {
					file += value2.get<std::string_view>();
				}
			} else if(how == write_enum_how::normal) {
				file += value2.get<std::string_view>();
			}

			if(j < num2-1) {
				file += ',';
			}

			if(how == write_enum_how::flags) {
				file += " //"sv;
				file += value2.get<std::string_view>();
			}

			file += '\n';
		}
	}

	bool vmod::load_late() noexcept
	{
		using namespace std::literals::string_view_literals;

		if(!bindings()) {
			return false;
		}

		{
			std::filesystem::path game_docs{root_dir/"docs"sv/"game"sv};
			write_docs(game_docs, game_vscript_class_bindings, false);
			write_docs(game_docs, game_vscript_func_bindings, false);

			gsdk::HSCRIPT const_table;
			if(vm_->GetValue(nullptr, "Constants", &const_table)) {
				std::string file;

				int num{vm_->GetNumTableEntries(const_table)};
				for(int i{0}, it{0}; i < num && it != -1; ++i) {
					script_variant_t key;
					script_variant_t value;
					it = vm_->GetKeyValue(const_table, it, &key, &value);

					std::string_view enum_name{key.get<std::string_view>()};

					if(enum_name[0] == 'E' || enum_name[0] == 'F') {
						file += "enum class "sv;
					} else {
						file += "namespace "sv;
					}
					file += enum_name;
					file += "\n{\n"sv;

					gsdk::HSCRIPT enum_table{value.get<gsdk::HSCRIPT>()};
					write_enum_table(file, 1, enum_table, enum_name[0] == 'F' ? write_enum_how::flags : write_enum_how::normal);

					file += '}';

					if(enum_name[0] == 'E' || enum_name[0] == 'F') {
						file += ';';
					}

					file += "\n\n"sv;
				}
				if(num > 0) {
					file.erase(file.end()-1, file.end());
				}

				std::filesystem::path doc_path{game_docs};
				doc_path /= "Constants"sv;
				doc_path.replace_extension(".txt"sv);

				write_file(doc_path, reinterpret_cast<const unsigned char *>(file.c_str()), file.length());
			}

			std::filesystem::path vmod_docs{root_dir/"docs"sv/"vmod"sv};
			write_vmod_docs(vmod_docs);
		}

		vmod_refresh_plugins();

		return true;
	}

	void vmod::map_loaded(std::string_view name) noexcept
	{
		is_map_loaded = true;

		for(const auto &pl : plugins) {
			if(!*pl) {
				continue;
			}

			pl->map_loaded(name);
		}
	}

	void vmod::map_active() noexcept
	{
		is_map_active = true;

		for(const auto &pl : plugins) {
			if(!*pl) {
				continue;
			}

			pl->map_active();
		}
	}

	void vmod::map_unloaded() noexcept
	{
		if(is_map_loaded) {
			for(const auto &pl : plugins) {
				if(!*pl) {
					continue;
				}

				pl->map_unloaded();
			}
		}

		is_map_loaded = false;
		is_map_active = false;
	}

	void vmod::game_frame([[maybe_unused]] bool) noexcept
	{
	#if 0
		vm_->Frame(sv_globals->frametime);
	#endif

		for(const auto &pl : plugins) {
			if(!*pl) {
				continue;
			}

			pl->game_frame();
		}
	}

	void vmod::unload() noexcept
	{
		if(old_spew) {
			SpewOutputFunc(old_spew);
		}

		vmod_unload_plugins();

		if(vm_) {
			unbindings();
		}

		vmod_reload_plugins.unregister();
		vmod_unload_plugins.unregister();
		vmod_unload_plugin.unregister();
		vmod_load_plugin.unregister();
		vmod_list_plugins.unregister();
		vmod_refresh_plugins.unregister();

		if(cvar_dll_id_ != gsdk::INVALID_CVAR_DLL_IDENTIFIER) {
			cvar->UnregisterConCommands(cvar_dll_id_);
		}

		if(vm_) {
			if(to_string_func && to_string_func != gsdk::INVALID_HSCRIPT) {
				vm_->ReleaseFunction(to_string_func);
			}

			if(to_int_func && to_int_func != gsdk::INVALID_HSCRIPT) {
				vm_->ReleaseFunction(to_int_func);
			}

			if(to_float_func && to_float_func != gsdk::INVALID_HSCRIPT) {
				vm_->ReleaseFunction(to_float_func);
			}

			if(to_bool_func && to_bool_func != gsdk::INVALID_HSCRIPT) {
				vm_->ReleaseFunction(to_bool_func);
			}

			if(typeof_func && typeof_func != gsdk::INVALID_HSCRIPT) {
				vm_->ReleaseFunction(typeof_func);
			}

			if(funcisg_func && funcisg_func != gsdk::INVALID_HSCRIPT) {
				vm_->ReleaseFunction(funcisg_func);
			}

			if(base_script && base_script != gsdk::INVALID_HSCRIPT) {
				vm_->ReleaseScript(base_script);
			}

			if(base_script_scope && base_script_scope != gsdk::INVALID_HSCRIPT) {
				vm_->ReleaseScope(base_script_scope);
			}

			if(server_init_script && server_init_script != gsdk::INVALID_HSCRIPT) {
				vm_->ReleaseScript(server_init_script);
			}

			if(scope_ && scope_ != gsdk::INVALID_HSCRIPT) {
				vm_->ReleaseScope(scope_);
			}

			vsmgr->DestroyVM(vm_);

			if(*g_pScriptVM == vm_) {
				*g_pScriptVM = nullptr;
			}
		}
	}
}

namespace vmod
{
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
	class vsp final : public gsdk::IServerPluginCallbacks
	{
	public:
		inline vsp() noexcept
		{
			load_return = vmod.load();
		}

		inline ~vsp() noexcept
		{
			if(!unloaded) {
				vmod.unload();
			}
		}

	private:
		const char *GetPluginDescription() override;
		bool Load(gsdk::CreateInterfaceFn, gsdk::CreateInterfaceFn) override;
		void Unload() override;
		void GameFrame(bool simulating) override;
		void ServerActivate([[maybe_unused]] gsdk::edict_t *edicts, [[maybe_unused]] int num_edicts, [[maybe_unused]] int max_clients) override;
		void LevelInit(const char *name) override;
		void LevelShutdown() override;

		bool load_return;
		bool unloaded;
	};
	#pragma GCC diagnostic pop

	const char *vsp::GetPluginDescription()
	{ return "vmod"; }

	bool vsp::Load(gsdk::CreateInterfaceFn, gsdk::CreateInterfaceFn)
	{
		if(!load_return) {
			return false;
		}

		if(!vmod.load_late()) {
			return false;
		}

		return true;
	}

	void vsp::Unload()
	{
		vmod.unload();
		unloaded = true;
	}

	void vsp::GameFrame(bool simulating)
	{ vmod.game_frame(simulating); }

	void vsp::ServerActivate([[maybe_unused]] gsdk::edict_t *edicts, [[maybe_unused]] int num_edicts, [[maybe_unused]] int max_clients)
	{ vmod.map_active(); }

	void vsp::LevelInit(const char *name)
	{ vmod.map_loaded(name); }

	void vsp::LevelShutdown()
	{ vmod.map_unloaded(); }

	static vsp vsp;
}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#endif
extern "C" __attribute__((__visibility__("default"))) void * __attribute__((__cdecl__)) CreateInterface(const char *name, int *status)
{
	using namespace gsdk;

	if(std::strncmp(name, IServerPluginCallbacks::interface_name.data(), IServerPluginCallbacks::interface_name.length()) == 0) {
		if(status) {
			*status = IFACE_OK;
		}
		return static_cast<IServerPluginCallbacks *>(&vmod::vsp);
	} else {
		if(status) {
			*status = IFACE_FAILED;
		}
		return nullptr;
	}
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif
