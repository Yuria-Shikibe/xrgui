module;

#include <cassert>

module mo_yanxi.gui.assets.manager;

import mo_yanxi.graphic.image_atlas;
import mo_yanxi.graphic.msdf;
import std;

namespace mo_yanxi::gui::assets::round_square{
namespace{
[[nodiscard]] constexpr float fixed_scale() noexcept{
	return 4.f;
}

[[nodiscard]] constexpr float max_radius() noexcept{
	return 40.f;
}

[[nodiscard]] constexpr math::usize2 round_square_extent() noexcept{
	return {96u, 96u};
}

void validate_key(const key value){
	if(value.radius.raw == 0){
		throw std::invalid_argument{"round square radius must be positive"};
	}
	if(value.radius.value() > max_radius()){
		throw std::invalid_argument{"round square radius is too large"};
	}

	switch(value.shape_mode){
	case mode::solid:
		if(value.attribute.raw != 0){
			throw std::invalid_argument{"solid round square does not accept an attribute"};
		}
		break;
	case mode::stroke:
		if(value.attribute.raw == 0){
			throw std::invalid_argument{"stroke round square width must be positive"};
		}
		if(value.attribute.raw > value.radius.raw){
			throw std::invalid_argument{"stroke round square width must not exceed radius"};
		}
		break;
	default:
		throw std::invalid_argument{"unknown round square mode"};
	}
}

[[nodiscard]] std::array<char, 6 + sizeof(resource_id)> make_atlas_name(const resource_id id) noexcept{
	static_assert(6 + sizeof(resource_id) <= 15);
	std::array<char, 6 + sizeof(resource_id)> result{};
	std::ranges::copy(std::string_view{"round:"}, result.begin());
	const auto bytes = std::bit_cast<std::array<char, sizeof(resource_id)>>(id);
	std::ranges::copy(bytes, result.begin() + 6);
	return result;
}

[[nodiscard]] graphic::sdf_load make_load_description(const key value){
	const auto radius = static_cast<double>(value.radius.value());
	switch(value.shape_mode){
	case mode::solid:
		return {
			graphic::msdf::msdf_generator{graphic::msdf::create_solid_border(radius)},
			round_square_extent(),
			3
		};
	case mode::stroke:
		return {
			graphic::msdf::msdf_generator{
				graphic::msdf::create_border(radius, static_cast<double>(value.attribute.value()))
			},
			round_square_extent(),
			3
		};
	default:
		std::unreachable();
	}
}

[[nodiscard]] image_nine_region make_nine_region(
	const constant_image_region_borrow& image_region,
	const key value
){
	const float border = static_cast<float>(graphic::msdf::sdf_image_border);
	const float edge = value.radius.value() + border * 1.5f;
	const auto extent = image_region->uv.get_region().extent().as<float>();
	return {
		image_region,
		math::frect{
			tags::unchecked,
			tags::from_extent,
			math::vec2{edge, edge},
			extent - math::vec2{edge * 2.f, edge * 2.f}
		},
		border
	};
}
}

fixed2 make_fixed2(const float value){
	if(!std::isfinite(value) || value < 0.f){
		throw std::invalid_argument{"round square fixed2 value must be finite and non-negative"};
	}
	const float scaled = value * fixed_scale();
	const float rounded = std::round(scaled);
	if(std::abs(scaled - rounded) > 0.0001f){
		throw std::invalid_argument{"round square fixed2 value must be quantized to 0.25"};
	}
	if(rounded > static_cast<float>(std::numeric_limits<std::uint16_t>::max())){
		throw std::invalid_argument{"round square fixed2 value is too large"};
	}
	return fixed2{static_cast<std::uint16_t>(rounded)};
}

key from_id(const resource_id id){
	if((id & reserved_mask) != 0){
		throw std::invalid_argument{"round square id has non-zero reserved bits"};
	}
	key result{
		.shape_mode = static_cast<mode>((id >> mode_shift) & 0xFF),
		.radius = fixed2{static_cast<std::uint16_t>((id >> radius_shift) & field_mask)},
		.attribute = fixed2{static_cast<std::uint16_t>((id >> attribute_shift) & field_mask)}
	};
	validate_key(result);
	return result;
}

key base_key(const float radius){
	key result{
		.shape_mode = mode::solid,
		.radius = make_fixed2(radius),
		.attribute = {}
	};
	validate_key(result);
	return result;
}

key border_key(const float radius, const float width){
	key result{
		.shape_mode = mode::stroke,
		.radius = make_fixed2(radius),
		.attribute = make_fixed2(width)
	};
	validate_key(result);
	return result;
}

const image_nine_region& get(const key value){
	return global::resource_manager().get_round_square(value);
}

const image_nine_region& get(const resource_id id){
	return global::resource_manager().get_round_square(id);
}

const image_nine_region& base(const float radius){
	return get(base_key(radius));
}

const image_nine_region& border(const float radius, const float width){
	return get(border_key(radius, width));
}

const image_nine_region& thin_border(const float radius){
	return border(radius, 1.f);
}

void bind_image_page(graphic::image_page& page) noexcept{
	global::resource_manager().bind_round_square_image_page(page);
}

void clear() noexcept{
	global::resource_manager().clear_round_square();
}
}

namespace mo_yanxi::gui::assets{

void resource_manager::bind_round_square_image_page(graphic::image_page& page) noexcept{
	std::lock_guard lock{round_square_mutex_};
	round_square_atlas_page_ = std::addressof(page);
}

void resource_manager::clear_round_square() noexcept{
	std::lock_guard lock{round_square_mutex_};
	if(round_square_page_ != nullptr){
		for(const auto id : round_square_cache_ | std::views::keys){
			round_square_page_->erase(id);
		}
	}
	round_square_cache_.clear();
	round_square_atlas_page_ = nullptr;
}

const image_nine_region& resource_manager::get_round_square(const round_square::key value){
	round_square::validate_key(value);
	const auto id = round_square::to_id(value);

	std::lock_guard lock{round_square_mutex_};
	if(auto itr = round_square_cache_.find(id); itr != round_square_cache_.end()){
		return itr->second;
	}

	if(round_square_page_ == nullptr){
		throw std::logic_error{"round square resource page is not initialized"};
	}

	if(auto handle = (*round_square_page_)[id]){
		auto [itr, inserted] = round_square_cache_.try_emplace(
			id,
			round_square::make_nine_region(*handle, value)
		);
		return itr->second;
	}

	if(round_square_atlas_page_ == nullptr){
		throw std::logic_error{"round square atlas page is not bound"};
	}

	const auto name = round_square::make_atlas_name(id);
	auto registered = round_square_atlas_page_->register_named_region(
		std::string_view{name.data(), name.size()},
		round_square::make_load_description(value),
		true
	);
	const auto handle = round_square_page_->insert_or_assign(id, registered.region);
	auto [itr, inserted] = round_square_cache_.try_emplace(
		id,
		round_square::make_nine_region(handle, value)
	);
	return itr->second;
}

const image_nine_region& resource_manager::get_round_square(const resource_id id){
	return get_round_square(round_square::from_id(id));
}
}

namespace mo_yanxi::gui::global{

assets::assets_page* builtin_page_{};

U u;

assets::resource_manager& resource_manager() noexcept{
	assert(global::builtin_page_ != nullptr && "GUI Resource Manager Not Initialized Yet");
	return u.resource_manager;
}

void initialize_assets_manager(mr::arena_id_t arena_id){
	if(builtin_page_){
		throw assets::duplicated_error{"GUI Resource Manager Already Initialized"};
	}
	std::construct_at(&u.resource_manager, arena_id);
	builtin_page_ = std::addressof(u.resource_manager.create_page(assets::builtin::page_name));
}

bool terminate_assets_manager() noexcept{
	if(builtin_page_){
		std::destroy_at(&u.resource_manager);
		builtin_page_ = nullptr;
		return true;
	}
	return false;
}

}

namespace mo_yanxi::gui::assets::builtin{

assets_page& get_page() noexcept{
	assert(global::builtin_page_ != nullptr);
	return *global::builtin_page_;
}

}
