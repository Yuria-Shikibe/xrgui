//
// Created by Matrix on 2025/12/28.
//

export module mo_yanxi.graphic.draw.instruction.recorder;

import std;
import mo_yanxi.graphic.draw.instruction.general;
import mo_yanxi.user_data_entry;
import mo_yanxi.type_register;

namespace mo_yanxi::graphic::draw::instruction{


export
template <typename Alloc = std::allocator<std::byte>>
struct draw_record_storage{
private:
	std::vector<instruction_head, typename std::allocator_traits<Alloc>::template rebind_alloc<instruction_head>> heads_{};
	std::vector<std::byte, typename std::allocator_traits<Alloc>::template rebind_alloc<std::byte>> data_{};


	void push_bytes(const void* bytes, std::size_t size){
		const auto cur = data_.size();
		data_.resize(cur + size);
		std::memcpy(data_.data() + cur, bytes, size);
	}

	void push_bytes(const auto& val){
		this->push_bytes(std::addressof(val), sizeof(val));
	}

public:
	[[nodiscard]] constexpr draw_record_storage() = default;

	[[nodiscard]] explicit(false) constexpr draw_record_storage(const Alloc& alloc) : heads_(alloc), data_(alloc){}

	void clear() noexcept{
		heads_.clear();
		data_.clear();
	}

	template <known_instruction Instr>
	void push(const Instr& instr){
		heads_.push_back(instruction::make_instruction_head(instr));
		this->push_bytes(instr);
	}

	std::span<const instruction_head> heads() const noexcept{
		return heads_;
	}

	const std::byte* data() const noexcept{
		return data_.data();
	}

	//TODO a good way to submit uniform buffer
	// template <typename Instr, typename C1, typename C2>
	// 	requires (!known_instruction<Instr>)
	// void push(const Instr& instr, const data_layout_table<C1>& table_vertex_only, const data_layout_table<C2>& table_general){
	// 	static constexpr type_identity_index tidx = unstable_type_identity_of<Instr>();
	// 	static constexpr bool vtx_only = is_vertex_stage_only<Instr>;
	//
	// 	std::uint32_t idx;
	// 	if constexpr (vtx_only){
	// 		const auto* ientry = table_vertex_only[tidx];
	// 		idx = ientry - table_vertex_only.begin();
	//
	// 		if(idx >= table_vertex_only.size()){
	// 			throw std::out_of_range("index out of range");
	// 		}
	// 	}else{
	// 		const auto* ientry = table_general[tidx];
	// 		idx = ientry - table_general.begin();
	//
	// 		if(idx >= table_general.size()){
	// 			throw std::out_of_range("index out of range");
	// 		}
	// 	}
	//
	// 	const auto head = instruction::instruction_head{
	// 		.type = instruction::instr_type::uniform_update,
	// 		.payload_size = instruction::get_payload_size<Instr>(),
	// 		.payload = {.ubo = instruction::user_data_indices{idx, !vtx_only}}
	// 	};
	// 	batch_backend_interface_.push(head, reinterpret_cast<const std::byte*>(&instr));
	// }
};

}