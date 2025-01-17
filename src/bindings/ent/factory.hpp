#pragma once

#include "../../gsdk/server/baseentity.hpp"
#include "../../plugin.hpp"
#include <vector>
#include <string>
#include <string_view>

namespace vmod::bindings::ent
{
	class factory_ref : public plugin::owned_instance
	{
		friend class singleton;
		friend void write_docs(const std::filesystem::path &) noexcept;

	public:
		~factory_ref() noexcept override;

		static bool bindings() noexcept;
		static void unbindings() noexcept;

	protected:
		inline factory_ref(gsdk::IEntityFactory *factory_) noexcept
			: factory{factory_}
		{
		}

		static vscript::class_desc<factory_ref> desc;

	private:
		inline bool initialize() noexcept
		{ return register_instance(&desc); }

		gsdk::IServerNetworkable *script_create(std::string_view classname) noexcept;

		inline std::size_t script_size() const noexcept
		{ return factory->GetEntitySize(); }

		gsdk::IEntityFactory *factory;

	private:
		factory_ref() = delete;
		factory_ref(const factory_ref &) = delete;
		factory_ref &operator=(const factory_ref &) = delete;
		factory_ref(factory_ref &&) = delete;
		factory_ref &operator=(factory_ref &&) = delete;
	};

	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
	class factory_impl final : public gsdk::IEntityFactory, public factory_ref
	{
		friend class singleton;
		friend void write_docs(const std::filesystem::path &) noexcept;

	public:
		~factory_impl() noexcept override;

		static bool bindings() noexcept;
		static void unbindings() noexcept;

	private:
		static vscript::class_desc<factory_impl> desc;

		bool initialize(std::string_view name, gsdk::HSCRIPT callback_) noexcept;

		gsdk::IServerNetworkable *script_create_sized(std::string_view classname, std::size_t size_) noexcept;

		inline factory_impl() noexcept
			: factory_ref{this}
		{
		}

		gsdk::HSCRIPT callback{gsdk::INVALID_HSCRIPT};
		std::size_t size{0};
		std::vector<std::string> names;

		gsdk::IServerNetworkable *create(std::string_view classname, std::size_t size_) noexcept;

		gsdk::IServerNetworkable *Create(const char *classname) override;
		void Destroy(gsdk::IServerNetworkable *net) override;
		size_t GetEntitySize() override;

	private:
		factory_impl(gsdk::IEntityFactory *) noexcept = delete;
		factory_impl(const factory_impl &) = delete;
		factory_impl &operator=(const factory_impl &) = delete;
		factory_impl(factory_impl &&) = delete;
		factory_impl &operator=(factory_impl &&) = delete;
	};
	#pragma GCC diagnostic pop
}
