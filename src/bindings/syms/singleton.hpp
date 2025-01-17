#pragma once

#include "../../vscript/vscript.hpp"
#include "../../vscript/class_desc.hpp"
#include "../../vscript/singleton_class_desc.hpp"
#include "../../symbol_cache.hpp"
#include "../../plugin.hpp"
#include <string_view>

namespace vmod
{
	class main;
}

namespace vmod::bindings::syms
{
	class singleton
	{
		friend class vmod::main;
		friend void write_docs(const std::filesystem::path &) noexcept;

	public:
		inline singleton(std::string_view name_) noexcept
			: name{name_}
		{
		}

		static bool bindings() noexcept;
		static void unbindings() noexcept;

		virtual ~singleton() noexcept;

		//TODO!!! remove plugin::owned_instance
		class qualification_it final : public plugin::owned_instance
		{
			friend class singleton;
			friend void write_docs(const std::filesystem::path &) noexcept;

		public:
			static bool bindings() noexcept;
			static void unbindings() noexcept;

			inline qualification_it(symbol_cache::const_iterator it_) noexcept
				: it{it_}
			{
			}

			~qualification_it() noexcept override;

		private:
			static vscript::class_desc<qualification_it> desc;

			inline bool initialize() noexcept
			{ return register_instance(&desc); }

			gsdk::HSCRIPT script_lookup(std::string_view symname) const noexcept;

			inline std::string_view script_name() const noexcept
			{ return it->first; }

			symbol_cache::const_iterator it;

		private:
			qualification_it(const qualification_it &) = delete;
			qualification_it &operator=(const qualification_it &) = delete;
			qualification_it(qualification_it &&) = delete;
			qualification_it &operator=(qualification_it &&) = delete;
		};

		//TODO!!! remove plugin::owned_instance
		class name_it final : public plugin::owned_instance
		{
			friend class singleton;
			friend void write_docs(const std::filesystem::path &) noexcept;

		public:
			static bool bindings() noexcept;
			static void unbindings() noexcept;

			inline name_it(symbol_cache::qualification_info::const_iterator it_) noexcept
				: it{it_}
			{
			}

			~name_it() noexcept override;

		private:
			static vscript::class_desc<name_it> desc;

			inline bool initialize() noexcept
			{ return register_instance(&desc); }

			gsdk::HSCRIPT script_lookup(std::string_view symname) const noexcept;

			inline std::string_view script_name() const noexcept
			{ return it->first; }
			inline void *script_addr() const noexcept
			{ return it->second->addr<void *>(); }
			inline generic_func_t script_func() const noexcept
			{ return it->second->func<generic_func_t>(); }
			inline generic_mfp_t script_mfp() const noexcept
			{ return it->second->mfp<generic_mfp_t>(); }
			inline std::size_t script_size() const noexcept
			{ return it->second->size(); }
			inline std::size_t script_vindex() const noexcept
			{ return it->second->virtual_index(); }

			symbol_cache::qualification_info::const_iterator it;

		private:
			name_it(const name_it &) = delete;
			name_it &operator=(const name_it &) = delete;
			name_it(name_it &&) = delete;
			name_it &operator=(name_it &&) = delete;
		};

	private:
		static vscript::singleton_class_desc<singleton> desc;

	protected:
		bool initialize() noexcept;

	private:
		static gsdk::HSCRIPT script_lookup_shared(symbol_cache::const_iterator it) noexcept;
		static gsdk::HSCRIPT script_lookup_shared(symbol_cache::qualification_info::const_iterator it) noexcept;

		gsdk::HSCRIPT script_lookup(std::string_view symname) const noexcept;
		gsdk::HSCRIPT script_lookup_global(std::string_view symname) const noexcept;

		virtual const symbol_cache &cache() const noexcept = 0;

		gsdk::HSCRIPT instance{gsdk::INVALID_HSCRIPT};

		std::string_view name;

	private:
		singleton(const singleton &) = delete;
		singleton &operator=(const singleton &) = delete;
		singleton(singleton &&) = delete;
		singleton &operator=(singleton &&) = delete;
	};

	class sv final : public singleton
	{
	public:
		inline sv() noexcept
			: singleton{"sv"}
		{
		}

		const symbol_cache &cache() const noexcept override;
	};
}
