//

//

export module mo_yanxi.graphic.compositor.post_process_pass_with_ubo;

import std;
import mo_yanxi.meta_programming;
export import mo_yanxi.graphic.compositor.post_process_pass;

namespace mo_yanxi::graphic::compositor{
export
template <typename ...Ts>
struct post_process_pass_with_ubo : post_process_stage{
	using value_type = std::tuple<Ts...>;

	using post_process_stage::post_process_stage;

private:
	value_type uniform_data_{};

	template <std::size_t Idx>
	ubo_subrange get_ubo_subrange() const {
		using tupTy = std::tuple_element_t<Idx, value_type>;
		auto entryValue = uniform_subranges_[Idx];
		if(entryValue.size != sizeof(tupTy)){
			throw std::runtime_error("uniform_data size mismatch");
		}
		return entryValue;
	}

	template <std::size_t Idx>
	void write_data_to_buffer(vk::buffer_mapper<vk::uniform_buffer>& buffer_mapper, const std::uint32_t frame_slot) const {
		ubo_subrange rng = get_ubo_subrange<Idx>();
		const auto& value = std::get<Idx>(uniform_data_);
		buffer_mapper.load(value, this->uniform_frame_stride_ * frame_slot + rng.offset);
	}

protected:

	void prepare(const vk::allocator_usage& alloc,
		const pass_data& pass,
		const math::u32size2 extent,
		const std::uint32_t frame_count) override{
		post_process_stage::prepare(alloc, pass, extent, frame_count);

		vk::buffer_mapper mapper{uniform_buffer_};
		for(std::uint32_t frame_slot = 0; frame_slot < this->frame_count_; ++frame_slot){
			[&]<std::size_t ...Idx>(std::index_sequence<Idx...>){
				(this->template write_data_to_buffer<Idx>(mapper, frame_slot), ...);
			}(std::index_sequence_for<Ts...>{});
		}
	}

public:
	template <typename T>
	void set_ubo_value(const T& value){
		static constexpr auto Idx = tuple_index_v<T, value_type>;
		static_assert(Idx < sizeof...(Ts), "Parameter Type Not Found");
		auto& cur_value = std::get<Idx>(uniform_data_);
		if constexpr (std::equality_comparable<T>){
			if(cur_value == value){
				return;
			}
		}

		cur_value = value;
		vk::buffer_mapper mapper{uniform_buffer_};
		for(std::uint32_t frame_slot = 0; frame_slot < this->frame_count_; ++frame_slot){
			this->template write_data_to_buffer<Idx>(mapper, frame_slot);
		}
	}

	template <typename C, typename T>
	void set_ubo_value(T C::* mptr, const T& value){
		static constexpr auto Idx = tuple_index_v<C, value_type>;
		static_assert(Idx < sizeof...(Ts), "Parameter Type Not Found");
		auto& cur_value = std::get<Idx>(uniform_data_).*mptr;
		if constexpr (std::equality_comparable<T>){
			if(cur_value == value){
				return;
			}
		}

		cur_value = value;
		vk::buffer_mapper mapper{uniform_buffer_};
		for(std::uint32_t frame_slot = 0; frame_slot < this->frame_count_; ++frame_slot){
			this->template write_data_to_buffer<Idx>(mapper, frame_slot);
		}
	}

	template <typename T, typename S>
	auto& get_ubo_value(this S& self) noexcept{
		static constexpr auto Idx = tuple_index_v<T, value_type>;
		return std::get<Idx>(self.uniform_data_);
	}

};
}
