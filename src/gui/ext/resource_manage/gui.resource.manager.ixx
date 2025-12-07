export module mo_yanxi.gui.assets.manager;

import mo_yanxi.gui.alloc;
export import mo_yanxi.gui.assets.image_regions;
import mo_yanxi.heterogeneous;
import mo_yanxi.graphic.image_region;
import mo_yanxi.graphic.image_region.borrow;
import mo_yanxi.algo;

import std;

namespace mo_yanxi::gui::assets{
export using resource_id = std::size_t;


struct duplicated_error : std::invalid_argument{
	[[nodiscard]] explicit duplicated_error(const std::string& _Message)
	: invalid_argument(_Message){
	}

	[[nodiscard]] explicit duplicated_error(const char* _Message)
	: invalid_argument(_Message){
	}
};

struct image_page{
	struct image_entry;

	using alias_map = string_hash_map<
		std::pair<const resource_id, image_entry>*,
		mr::heap_allocator<std::pair<const std::string, std::pair<const resource_id, image_entry>*>>>;

	struct image_entry{
		image_region_borrow image_borrow;
		mr::heap_vector<alias_map::iterator> references_to_this;

		[[nodiscard]] explicit image_entry(
			const mr::heap_allocator<alias_map::iterator>& allocator, const image_region_borrow& image_region)
		: image_borrow(image_region), references_to_this(allocator){
		}
	};

	mutable resource_id last_id_{};
private:
	std::unordered_map<
		resource_id, image_entry,
		std::hash<resource_id>, std::equal_to<resource_id>,
		mr::heap_allocator<std::pair<const resource_id, image_entry>>> image_regions_;

	alias_map alias_map_;

public:
	[[nodiscard]] resource_id acquire_id() const noexcept{
		//Reserve 0 currently
		return ++last_id_;
	}

	[[nodiscard]] explicit image_page(
		const mr::heap_allocator<image_page>& allocator) :
	image_regions_(allocator),
	alias_map_(allocator){
	}

	image_region_borrow& insert(resource_id id, const image_region_borrow& region){
		auto [itr, suc] = image_regions_.try_emplace(id, mr::heap_allocator<alias_map::iterator>{image_regions_.get_allocator()}, region);
		if(!suc)throw duplicated_error{"Image ID Duplicated"};
		return itr->second.image_borrow;
	}

	bool erase(resource_id id) noexcept {
		if(auto itr = image_regions_.find(id); itr != image_regions_.end()){
			for (const auto & references_to_thi : itr->second.references_to_this){
				alias_map_.erase(references_to_thi);
			}
			image_regions_.erase(itr);
			return true;
		}
		return false;
	}

	template <typename T>
		requires (std::constructible_from<std::string, T&&>)
	void add_alias(resource_id id, T&& string_prov){
		if(auto itr = image_regions_.find(id); itr != image_regions_.end()){
			std::pair<alias_map::iterator, bool> rst = alias_map_.try_emplace(std::forward<T>(string_prov), std::to_address(itr));
			if(!rst.second){
				throw duplicated_error{"Image Alias Duplicated"};
			}
		}
	}

	bool erase_alias(std::string_view alias) noexcept{
		if(auto itr = alias_map_.find(alias); itr != alias_map_.end()){
			algo::erase_unique_unstable(itr->second->second.references_to_this, itr);
			alias_map_.erase(itr);
			return true;
		}
		return false;
	}

	std::optional<resource_id> get_relevant_id(std::string_view alias) const noexcept{
		if(auto* p = alias_map_.try_find(alias)){
			return (*p)->first;
		}
		return std::nullopt;
	}

	[[nodiscard]] std::optional<image_region_borrow> operator[](std::string_view alias) const noexcept{
		if(auto* p = alias_map_.try_find(alias)){
			return (*p)->second.image_borrow;
		}
		return std::nullopt;
	}

	[[nodiscard]] std::optional<image_region_borrow> operator[](resource_id id) const noexcept{
		if(auto itr = image_regions_.find(id); itr != image_regions_.end()){
			return itr->second.image_borrow;
		}
		return std::nullopt;
	}

	[[nodiscard]] const image_region_borrow* find(resource_id id) const noexcept{
		if(auto itr = image_regions_.find(id); itr != image_regions_.end()){
			return std::addressof(itr->second.image_borrow);
		}
		return nullptr;
	}

	[[nodiscard]] const image_region_borrow* find(std::string_view alias) const noexcept{
		if(auto* p = alias_map_.try_find(alias)){
			return std::addressof((*p)->second.image_borrow);
		}
		return nullptr;
	}


};


export
struct resource_manager{
private:
	mr::heap heap_{};
	string_hash_map<image_page, mr::heap_allocator<std::pair<const std::string, image_page>>> pages_{};

public:
	[[nodiscard]] explicit resource_manager(const mr::arena_id_t arena_id)
	: heap_(arena_id, 1), pages_(mr::heap_allocator{heap_.get()}){
	}

	template <typename T>
		requires (std::constructible_from<std::string, T&&> && std::constructible_from<std::string_view, const T&>)
	image_page& create_page(T&& page_name){
		std::string_view str{page_name};
		if(str.contains(':')){
			throw std::invalid_argument{"page_name should not contains ':'"};
		}

		std::pair<decltype(pages_)::iterator, bool> rst = pages_.try_emplace(std::forward<T>(page_name), mr::heap_allocator<image_page>{pages_.get_allocator()});
		if(!rst.second){
			throw duplicated_error{"Image Page Duplicated"};
		}

		return rst.first->second;
	}

	[[nodiscard]] std::optional<image_region_borrow> operator[](std::string_view full_name) const noexcept{
		auto pos = full_name.find(':');
		if(pos == std::string_view::npos)return {};
		return (*this)[full_name.substr(0, pos), full_name.substr(pos + 1)];
	}

	[[nodiscard]] std::optional<image_region_borrow> operator[](std::string_view page_name, std::string_view alias) const noexcept{
		if(auto p = pages_.try_find(page_name)){
			return p->operator[](alias);
		}
		return std::nullopt;
	}

	[[nodiscard]] std::optional<image_region_borrow> operator[](std::string_view page_name, resource_id id) const noexcept{
		if(auto p = pages_.try_find(page_name)){
			return p->operator[](id);
		}
		return std::nullopt;
	}
};

}



namespace mo_yanxi::gui{


namespace assets::builtin{
export inline constexpr auto page_name_str_array = std::to_array("__builtin");
export inline constexpr std::string_view page_name = {page_name_str_array.data(), page_name_str_array.size() - 1};

export
enum id_enum{
	white,
	row_patch_area,
	COUNT,
};

export
enum icon{
	close = COUNT,
	check,
	blender_icon_physics,
};

}


namespace global{

union U{
	assets::resource_manager resource_manager;

	[[nodiscard]] U(){}

	~U(){}
};

extern U u;
export
[[nodiscard]] assets::resource_manager& resource_manager() noexcept;

export
void initialize_assets_manager(mr::arena_id_t arena_id);

export
bool terminate_assets_manager() noexcept;
}

namespace assets::builtin{
export
[[nodiscard]] image_page& get_page() noexcept;
}
}

