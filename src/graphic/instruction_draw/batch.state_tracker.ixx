module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.graphic.draw.instruction.state_tracker;

import std;
export import mo_yanxi.graphic.draw.instruction.batch.common;
export import mo_yanxi.binary_trace;

namespace mo_yanxi::graphic::draw::instruction {

export
struct state_tracker {
	using tag_type = mo_yanxi::binary_diff_trace::tag;

private:
	struct depth_record{
		std::uint32_t tag;
		std::uint32_t cur_depth;
		std::uint32_t max_depth;
	};

	tagged_range_store records_{};
	std::vector<depth_record> depths_{};
	std::vector<tag_type> dirty_tags_{};

public:
	[[nodiscard]] state_tracker() = default;

	[[nodiscard]] std::span<const depth_record> get_depth_records() const noexcept{
		return depths_;
	}

	void update_depth(depth_op_type op, std::uint32_t tag_major){
		auto get_track = [&] -> auto&{
			if(auto itr = std::ranges::find(depths_, tag_major, &depth_record::tag); itr != depths_.end()){
				return *itr;
			}
			return depths_.emplace_back(tag_major);
		};

		switch(op){
		case depth_op_type::noop:
			break;
		case depth_op_type::incr: {
			auto& t = get_track();
			++t.cur_depth;
			t.max_depth = std::max(t.cur_depth, t.max_depth);
			break;
		}
		case depth_op_type::decr: {
			auto& t = get_track();
			assert(t.cur_depth != 0);
			--t.cur_depth;
			break;
		}
		}
	}

	template <std::size_t N>
	void clear_mask(const std::bitset<N>& mask) noexcept {
		records_.clear_mask(mask);
		if(mask.none()) return;
		std::erase_if(dirty_tags_, [&](tag_type tag){
			return mask.test(tag.major);
		});
	}

	void update(tag_type tag, std::span<const std::byte> payload, unsigned offset = 0) {
		if(payload.empty()) return;
		if(const auto* rec = records_.try_find_record(tag); rec){
			const auto storage = records_.data();
			auto existing = rec->src_span.to_span(storage.data());
			const unsigned rec_start = rec->logical_offset;
			const unsigned rec_end = rec_start + rec->src_span.size;
			if(offset >= rec_start && offset + payload.size() <= rec_end){
				if(std::memcmp(payload.data(), existing.data() + (offset - rec_start), payload.size()) == 0){
					return;
				}
			}
		}
		records_.push(tag, payload, offset);
		if(auto it = std::ranges::lower_bound(dirty_tags_, tag); it == dirty_tags_.end() || *it != tag){
			dirty_tags_.insert(it, tag);
		}
	}

	bool flush(section_state_delta_set& out_config) {
		bool has_changes = false;
		for(const auto& tag : dirty_tags_){
			if(const auto* rec = records_.try_find_record(tag); rec){
				const auto storage = records_.data();
				out_config.push(rec->tag, rec->src_span.to_span(storage.data()), rec->logical_offset);
				has_changes = true;
			}
		}
		dirty_tags_.clear();
		return has_changes;
	}

	void reset() noexcept {
		records_.clear();
		depths_.clear();
		dirty_tags_.clear();
	}
};

}
