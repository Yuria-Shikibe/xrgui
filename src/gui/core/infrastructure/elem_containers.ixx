module;

#include <gch/small_vector.hpp>
#include <beman/inplace_vector.hpp>

export module mo_yanxi.gui.elem_containers;

import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.alloc;
import std;

namespace mo_yanxi::gui{

export
template <typename Container>
struct elem_owning_container{
protected:
	Container data_;

public:
	template <typename... Args>
		requires (std::constructible_from<Container, Args&&...>)
	explicit elem_owning_container(Args&&... args)
		: data_(std::forward<Args>(args)...){}

	elem_owning_container(const elem_owning_container&) = delete;
	elem_owning_container& operator=(const elem_owning_container&) = delete;
	elem_owning_container(elem_owning_container&&) = default;
	elem_owning_container& operator=(elem_owning_container&&) = default;

	~elem_owning_container(){ clear(); }

	void push_back(elem_ptr&& p){ data_.push_back(p.release()); }

	elem& insert(std::size_t where, elem_ptr&& p){
		auto* raw = p.release();
		return **data_.insert(data_.begin() + where, raw);
	}

	[[nodiscard]] elem_ptr extract(std::size_t where){
		elem_ptr p{data_[where]};
		data_.erase(data_.begin() + where);
		return p;
	}

	[[nodiscard]] elem_ptr exchange(std::size_t where, elem_ptr&& p){
		elem_ptr old{data_[where]};
		data_[where] = p.release();
		return old;
	}

	void erase(std::size_t where){
		elem_ptr{data_[where]};
		data_.erase(data_.begin() + where);
	}

	void swap_with_ptr(std::size_t i, elem_ptr& p) noexcept{
		elem* tmp = p.release();
		p.reset(data_[i]);
		data_[i] = tmp;
	}

	void clear() noexcept{
		for(auto* e : data_){ elem_ptr tmp{e}; }
		data_.clear();
	}

	[[nodiscard]] std::span<elem* const> as_span() const noexcept{ return data_; }

	[[nodiscard]] std::size_t size()  const noexcept{ return data_.size(); }
	[[nodiscard]] bool        empty() const noexcept{ return data_.empty(); }
	[[nodiscard]] elem*  operator[](std::size_t i) const noexcept{ return data_[i]; }
	[[nodiscard]] elem*& operator[](std::size_t i)       noexcept{ return data_[i]; }
	[[nodiscard]] elem*  front() const noexcept{ return data_.front(); }
	[[nodiscard]] elem*  back()  const noexcept{ return data_.back(); }

	// C++23 deducing this: 4 const/non-const overloads → 2
	[[nodiscard]] auto begin(this auto& self) noexcept{ return self.data_.begin(); }
	[[nodiscard]] auto end  (this auto& self) noexcept{ return self.data_.end(); }
};

export
template <typename Alloc = mr::heap_allocator<elem*>>
struct basic_elem_vector : elem_owning_container<std::vector<elem*, Alloc>>{
	using allocator_type = Alloc;
	[[nodiscard]] explicit basic_elem_vector(Alloc alloc = {})
		: elem_owning_container<std::vector<elem*, Alloc>>(std::move(alloc)){}
	void reserve(std::size_t n){ this->data_.reserve(n); }
};

export using elem_vector = basic_elem_vector<>;

export
template <std::size_t N, typename Alloc = mr::unvs_allocator<elem*>>
struct basic_elem_small_vector : elem_owning_container<gch::small_vector<elem*, N, Alloc>>{
	using allocator_type = Alloc;
	[[nodiscard]] explicit basic_elem_small_vector(Alloc alloc = {})
		: elem_owning_container<gch::small_vector<elem*, N, Alloc>>(std::move(alloc)){}
	void reserve(std::size_t n){ this->data_.reserve(n); }
};

export
template <std::size_t N>
using elem_small_vector = basic_elem_small_vector<N>;

export
template <std::size_t N>
struct elem_inplace_vector : elem_owning_container<beman::inplace_vector::inplace_vector<elem*, N>>{
	elem_inplace_vector() = default;
};

export
template <std::size_t N>
struct elem_array{
private:
	std::array<elem_ptr, N> data_{};
	mutable std::array<elem*, N> raw_{};

public:
	[[nodiscard]] elem_ptr&       operator[](std::size_t i)       noexcept{ return data_[i]; }
	[[nodiscard]] const elem_ptr& operator[](std::size_t i) const noexcept{ return data_[i]; }

	[[nodiscard]] std::span<elem* const> as_span() const noexcept{
		std::ranges::transform(data_, raw_.begin(), [](const elem_ptr& p){ return p.get(); });
		return raw_;
	}

	[[nodiscard]] constexpr std::size_t size() const noexcept{ return N; }

	[[nodiscard]] auto begin(this auto& self) noexcept{ return self.data_.begin(); }
	[[nodiscard]] auto end  (this auto& self) noexcept{ return self.data_.end(); }
};

}
