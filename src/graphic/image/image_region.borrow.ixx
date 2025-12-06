//
// Created by Matrix on 2025/11/16.
//

export module mo_yanxi.graphic.image_region.borrow;

export import mo_yanxi.referenced_ptr;
import std;

namespace mo_yanxi::graphic{

/**
 * @brief Consider that the image region is immutable after allocation, this type erasure is valid.
 * @tparam T image region type
 */
export
template <typename T, typename Base>
struct universal_borrowed_image_region : referenced_ptr<Base, no_deletion_on_ref_count_to_zero>{
	using base_type = Base;
	using region_type = T;

private:
	template <typename Ty, typename BaseTy>
	friend struct universal_borrowed_image_region;

	region_type cache_{};

public:
	[[nodiscard]] universal_borrowed_image_region() = default;

	[[nodiscard]] universal_borrowed_image_region(
		base_type& target,
		const region_type& region
		)
		: referenced_ptr<Base, no_deletion_on_ref_count_to_zero>(std::addressof(target)), cache_(region){
	}

	template <typename Ty>
		requires (std::constructible_from<region_type, Ty>)
	[[nodiscard]] universal_borrowed_image_region(
		base_type& target,
		Ty&& region
		)
		: referenced_ptr<Base, no_deletion_on_ref_count_to_zero>(std::addressof(target)), cache_(std::forward<Ty>(region)){
	}

	template <typename Ty>
		requires requires(const Ty& t, region_type& region){
			region = static_cast<region_type>(t);
		}
	[[nodiscard]] explicit(!std::convertible_to<const Ty&, region_type>) universal_borrowed_image_region(
		const universal_borrowed_image_region<Ty, Base>& target
	) noexcept(std::is_nothrow_constructible_v<region_type, const Ty&>)
	: referenced_ptr<Base, no_deletion_on_ref_count_to_zero>(target), cache_(static_cast<region_type>(target.cache_)){

	}

	template <typename Ty>
		requires requires(Ty&& t, region_type& region){
			region = static_cast<region_type>(std::move(t));
		}
	[[nodiscard]] explicit(!std::convertible_to<const Ty&, region_type>) universal_borrowed_image_region(
		universal_borrowed_image_region<Ty, Base>&& target
	) noexcept(std::is_nothrow_constructible_v<region_type, Ty&&>)
	: referenced_ptr<Base, no_deletion_on_ref_count_to_zero>(std::move(target)), cache_(static_cast<region_type>(std::move(target.cache_))){}

	const region_type& operator*() const noexcept{
		return cache_;
	}

	const region_type* operator->() const noexcept{
		return std::addressof(cache_);
	}

};



}