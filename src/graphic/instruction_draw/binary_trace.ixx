export module mo_yanxi.binary_trace;

import std;

namespace mo_yanxi{
constexpr void memcpy_constexpr(std::byte* dst, const std::byte* src, std::size_t size) noexcept{
	if consteval{
		std::ranges::copy(src, src + size, dst);
	} else{
		std::memcpy(dst, src, size);
	}
}

export
struct binary_diff_trace{
	using flag_type = std::uint32_t;

	struct tag{
		flag_type major;
		flag_type minor;
		constexpr bool operator==(const tag&) const noexcept = default;
		constexpr auto operator<=>(const tag&) const noexcept = default;
	};

	struct sub_span{
		unsigned offset;
		unsigned size;

		[[nodiscard]] constexpr std::span<std::byte> to_span(std::byte* base) const noexcept{
			return {base + offset, size};
		}

		[[nodiscard]] constexpr std::span<const std::byte> to_span(const std::byte* base) const noexcept{
			return {base + offset, size};
		}

		constexpr explicit operator bool() const noexcept{
			return size != 0;
		}
	};

	struct export_record{
		tag tag;
		unsigned offset;
		std::span<const std::byte> range;
	};

	struct record{
		tag tag;
		sub_span src_span;
		unsigned logical_offset;
		unsigned count;
	};

private:
	enum class op_type : std::uint8_t{
		new_record,
		append,
		diff,
		realloc
	};

	struct entry{
		tag tag;
		op_type type;





		sub_span data_span;
		unsigned aux_val;
	};

	std::vector<std::byte> entry_diffs_{};
	std::vector<entry> entries_{};

	std::vector<std::byte> record_data_{};
	std::vector<record> records_{};


	[[nodiscard]] constexpr auto find_record_iter(tag t) noexcept{
		return std::ranges::find(records_, t, &record::tag);
	}

	[[nodiscard]] constexpr auto find_record_iter(tag t) const noexcept{
		return std::ranges::find(records_, t, &record::tag);
	}

public:

	constexpr void reserve(std::size_t total_bytes, std::size_t record_count){
		record_data_.reserve(total_bytes);
		records_.reserve(record_count);
		entries_.reserve(record_count * 2);
	}

	constexpr bool contains(tag t) const noexcept{
		return find_record_iter(t) != records_.end();
	}

	[[nodiscard]] constexpr std::optional<export_record> find_record(tag t) const noexcept{
		if(auto it = find_record_iter(t); it != records_.end()){
			return export_record{t, it->logical_offset, it->src_span.to_span(record_data_.data())};
		}
		return std::nullopt;
	}

	constexpr void push(tag tag, std::span<const std::byte> data, unsigned offset){
		auto itr = find_record_iter(tag);


		if(itr == records_.end()){
			const unsigned phys_off = static_cast<unsigned>(record_data_.size());

			record_data_.resize(phys_off + data.size());
			memcpy_constexpr(record_data_.data() + phys_off, data.data(), data.size());

			records_.emplace_back(tag, sub_span{phys_off, static_cast<unsigned>(data.size())}, offset, 1);
			entries_.emplace_back(tag, op_type::new_record, sub_span{0, 0}, 0);
			return;
		}

		record& rec = *itr;
		const unsigned req_start = offset;
		const unsigned req_end = offset + static_cast<unsigned>(data.size());
		const unsigned cur_start = rec.logical_offset;
		const unsigned cur_end = rec.logical_offset + rec.src_span.size;


		const unsigned new_start = std::min(req_start, cur_start);
		const unsigned new_end = std::max(req_end, cur_end);




		bool is_logical_append = (req_start == cur_end);
		bool is_phys_tail = (rec.src_span.offset + rec.src_span.size == record_data_.size());

		if(is_logical_append && is_phys_tail){
			unsigned append_size = data.size();
			unsigned old_phys_size = static_cast<unsigned>(record_data_.size());

			record_data_.resize(old_phys_size + append_size);
			memcpy_constexpr(record_data_.data() + old_phys_size, data.data(), append_size);


			rec.src_span.size += append_size;
			rec.count++;


			entries_.emplace_back(tag, op_type::append, sub_span{0, 0}, append_size);
			return;
		}



		if(req_start >= cur_start && req_end <= cur_end){
			unsigned rel_offset = req_start - cur_start;
			unsigned diff_off = static_cast<unsigned>(entry_diffs_.size());

			entry_diffs_.resize(diff_off + data.size());
			auto phys_span = rec.src_span.to_span(record_data_.data());


			for(size_t i = 0; i < data.size(); ++i){
				entry_diffs_[diff_off + i] = phys_span[rel_offset + i] ^ data[i];
			}

			memcpy_constexpr(phys_span.data() + rel_offset, data.data(), data.size());

			rec.count++;

			entries_.emplace_back(tag, op_type::diff, sub_span{diff_off, static_cast<unsigned>(data.size())},
				rel_offset);
			return;
		}



		{
			unsigned new_size = new_end - new_start;
			unsigned new_phys_off = static_cast<unsigned>(record_data_.size());


			entries_.emplace_back(tag, op_type::realloc, rec.src_span, rec.logical_offset);

			record_data_.resize(new_phys_off + new_size);
			std::byte* base = record_data_.data();


			if(rec.src_span.size > 0){
				unsigned old_rel = cur_start - new_start;
				memcpy_constexpr(base + new_phys_off + old_rel, base + rec.src_span.offset, rec.src_span.size);
			}


			unsigned new_rel = req_start - new_start;
			memcpy_constexpr(base + new_phys_off + new_rel, data.data(), data.size());


			rec.src_span = {new_phys_off, new_size};
			rec.logical_offset = new_start;
			rec.count++;
		}
	}

	constexpr void undo(tag tag){
		auto rec_it = find_record_iter(tag);
		if(rec_it == records_.end()) return;



		auto entry_it = std::ranges::find(entries_ | std::views::reverse, tag, &entry::tag);
		if(entry_it == (entries_ | std::views::reverse).end()) return;


		auto forward_it = std::prev(entry_it.base());
		const auto& last_entry = *forward_it;
		record& rec = *rec_it;

		switch(last_entry.type){
		case op_type::new_record :{


			records_.erase(rec_it);
			break;
		}
		case op_type::append :{

			unsigned appended_size = last_entry.aux_val;
			rec.src_span.size -= appended_size;
			rec.count--;

			if(rec.src_span.offset + rec.src_span.size + appended_size == record_data_.size()){
				record_data_.resize(record_data_.size() - appended_size);
			}
			break;
		}
		case op_type::diff :{

			auto diff_range = last_entry.data_span.to_span(entry_diffs_.data());
			auto rec_range = rec.src_span.to_span(record_data_.data());
			unsigned rel_offset = last_entry.aux_val;

			for(unsigned i = 0; i < diff_range.size(); ++i){
				rec_range[rel_offset + i] ^= diff_range[i];
			}


			if(last_entry.data_span.offset + last_entry.data_span.size == entry_diffs_.size()){
				entry_diffs_.resize(last_entry.data_span.offset);
			}
			rec.count--;
			break;
		}
		case op_type::realloc :{

			rec.src_span = last_entry.data_span;
			rec.logical_offset = last_entry.aux_val;
			rec.count--;
			break;
		}
		}

		entries_.erase(forward_it);
	}

	constexpr auto get_records() const noexcept{
		return records_ | std::views::transform(
			[p = this->record_data_.data()](const record& r) constexpr -> export_record{
				return {r.tag, r.logical_offset, r.src_span.to_span(p)};
			});
	}

	constexpr bool empty() const noexcept{
		return records_.empty();
	}

	constexpr void clear() noexcept{
		records_.clear();
		entries_.clear();
		record_data_.clear();
		entry_diffs_.clear();
	}
};

export
struct binary_config_trace{
	using tag = binary_diff_trace::tag;
	using sub_span = binary_diff_trace::sub_span;
	using record = binary_diff_trace::record;
	using export_record = binary_diff_trace::export_record;

	struct record_fix{
		unsigned logical_offset;
		sub_span data;

		std::span<const std::byte> to_span(const binary_config_trace& trace) const noexcept{
			return data.to_span(trace.buffer_.data());
		}
	};

private:
	std::vector<std::byte> record_data_{};
	std::vector<record> records_{};


	std::vector<std::byte> buffer_{};

	[[nodiscard]] constexpr auto find_record_iter(tag t) noexcept{
		return std::ranges::find(records_, t, &record::tag);
	}

	[[nodiscard]] constexpr auto find_record_iter(tag t) const noexcept{
		return std::ranges::find(records_, t, &record::tag);
	}

public:
	constexpr void clear_mask(const std::bitset<32>& mask) noexcept {
		if (mask.none()) return;
		std::erase_if(records_, [&](const record& rec) {
			return mask.test(rec.tag.major);
		});
	}

	constexpr void reserve(std::size_t total_bytes, std::size_t record_count){
		record_data_.reserve(total_bytes);
		records_.reserve(record_count);
	}

	constexpr bool contains(tag t) const noexcept{
		return find_record_iter(t) != records_.end();
	}


	constexpr auto get_records() const noexcept{
		return records_ | std::views::transform(
			[p = this->record_data_.data()](const record& r) constexpr -> export_record{
				return {r.tag, r.logical_offset, r.src_span.to_span(p)};
			});
	}

	[[nodiscard]] constexpr record_fix load_tag(tag t){
		auto it = find_record_iter(t);
		if(it == records_.end()){
			return {};
		}

		const record& rec = *it;
		const unsigned copy_size = rec.src_span.size;
		const unsigned buffer_offset = static_cast<unsigned>(buffer_.size());


		buffer_.resize(buffer_offset + copy_size);


		auto src_span = rec.src_span.to_span(record_data_.data());
		memcpy_constexpr(buffer_.data() + buffer_offset, src_span.data(), copy_size);

		return record_fix{
				rec.logical_offset,
				sub_span{buffer_offset, copy_size}
			};
	}

	constexpr void store_tag(tag t, record_fix r){
		auto it = find_record_iter(t);
		if(it == records_.end()){
			return;
		}




		const unsigned old_size = it->src_span.size;
		const unsigned old_offset = it->src_span.offset;
		const bool is_shrink_or_same = (r.data.size <= old_size);

		unsigned new_phys_off = old_offset;
		unsigned new_total_size = record_data_.size();
		bool requires_resize = false;

		if(!is_shrink_or_same){
			bool is_phys_tail = (old_offset + old_size == record_data_.size());
			if(is_phys_tail){
				new_total_size = old_offset + r.data.size;
			} else{
				new_phys_off = static_cast<unsigned>(record_data_.size());
				new_total_size = new_phys_off + r.data.size;
			}
			requires_resize = true;
		}




		if(requires_resize){



			record_data_.resize(new_total_size);
		}





		record& rec = *it;
		rec.logical_offset = r.logical_offset;
		auto buffer_span = r.data.to_span(buffer_.data());

		if(is_shrink_or_same){
			auto target_span = rec.src_span.to_span(record_data_.data());
			memcpy_constexpr(target_span.data(), buffer_span.data(), r.data.size);
			rec.src_span.size = r.data.size;
		} else{

			memcpy_constexpr(record_data_.data() + new_phys_off, buffer_span.data(), r.data.size);
			rec.src_span = {new_phys_off, r.data.size};
		}



		if(r.data.offset + r.data.size == buffer_.size()){
			buffer_.resize(r.data.offset);
		} else{
			if consteval{
				std::ranges::fill(buffer_span, std::byte{0});
			} else{
				std::memset(buffer_.data() + r.data.offset, 0, r.data.size);
			}
		}
	}

	constexpr void push(tag tag, std::span<const std::byte> data, unsigned offset){
		auto itr = find_record_iter(tag);


		if(itr == records_.end()){
			const unsigned phys_off = static_cast<unsigned>(record_data_.size());


			record_data_.resize(phys_off + data.size());
			memcpy_constexpr(record_data_.data() + phys_off, data.data(), data.size());

			records_.emplace_back(tag, sub_span{phys_off, static_cast<unsigned>(data.size())}, offset, 1);
			return;
		}

		record& rec = *itr;
		const unsigned req_start = offset;
		const unsigned req_end = offset + static_cast<unsigned>(data.size());
		const unsigned cur_start = rec.logical_offset;
		const unsigned cur_end = rec.logical_offset + rec.src_span.size;


		const unsigned new_start = std::min(req_start, cur_start);
		const unsigned new_end = std::max(req_end, cur_end);


		bool is_logical_append = (req_start == cur_end);
		bool is_phys_tail = (rec.src_span.offset + rec.src_span.size == record_data_.size());

		if(is_logical_append && is_phys_tail){
			unsigned append_size = data.size();
			unsigned old_phys_size = static_cast<unsigned>(record_data_.size());

			record_data_.resize(old_phys_size + append_size);
			memcpy_constexpr(record_data_.data() + old_phys_size, data.data(), append_size);


			rec.src_span.size += append_size;
			rec.count++;
			return;
		}



		if(req_start >= cur_start && req_end <= cur_end){
			unsigned rel_offset = req_start - cur_start;
			auto phys_span = rec.src_span.to_span(record_data_.data());


			memcpy_constexpr(phys_span.data() + rel_offset, data.data(), data.size());
			rec.count++;
			return;
		}



		{
			unsigned new_size = new_end - new_start;
			unsigned new_phys_off = static_cast<unsigned>(record_data_.size());

			record_data_.resize(new_phys_off + new_size);
			std::byte* base = record_data_.data();


			if(rec.src_span.size > 0){
				unsigned old_rel = cur_start - new_start;
				memcpy_constexpr(base + new_phys_off + old_rel, base + rec.src_span.offset, rec.src_span.size);
			}


			unsigned new_rel = req_start - new_start;
			memcpy_constexpr(base + new_phys_off + new_rel, data.data(), data.size());


			rec.src_span = {new_phys_off, new_size};
			rec.logical_offset = new_start;
			rec.count++;
		}
	}


	constexpr bool empty() const noexcept{
		return records_.empty();
	}



	constexpr void clear() noexcept{
		records_.clear();
		record_data_.clear();
		buffer_.clear();
	}
};

export
struct binary_config_guard{
private:
	binary_config_trace* trace_;
	binary_config_trace::record_fix fix_;
	binary_config_trace::tag tag_;

	void store() const noexcept {
		if(trace_ && fix_.data){
			try{
				trace_->store_tag(tag_, fix_);
			}catch(...){

			}
		}

	}
public:

	constexpr binary_config_guard(binary_config_trace& trace, const binary_config_trace::tag& tag, const std::span<const std::byte> payload, unsigned offset = 0)
		: trace_(&trace), fix_(trace.load_tag(tag)), tag_(tag){
		trace_->push(tag, payload, offset);
	}

	template <typename T>
		requires (std::is_trivially_copyable_v<T>)
	constexpr binary_config_guard(binary_config_trace& trace, const binary_config_trace::tag& tag, const T& payload, unsigned offset = 0)
		: binary_config_guard(trace, tag, {reinterpret_cast<const std::byte*>(std::addressof(payload)), sizeof(payload)}, offset){
	}

	binary_config_guard(const binary_config_guard& other) = delete;

	binary_config_guard(binary_config_guard&& other) noexcept
		: trace_(std::exchange(other.trace_, {})),
		fix_(std::exchange(other.fix_, {})),
		tag_(std::exchange(other.tag_, {})){
	}

	binary_config_guard& operator=(const binary_config_guard& other) = delete;

	binary_config_guard& operator=(binary_config_guard&& other) noexcept{
		if(this == &other) return *this;
		store();

		trace_ = std::exchange(other.trace_, {});
		fix_ = std::exchange(other.fix_, {});
		tag_ = std::exchange(other.tag_, {});
		return *this;
	}

	~binary_config_guard(){
		store();
	}
};


constexpr void assert_true(bool cond){
	if(!cond) throw "Assertion failed";
}

consteval bool test_optimized_trace(){
	binary_diff_trace trace;
	using Tag = binary_diff_trace::tag;
	Tag tagA{1, 1};

	std::array<std::byte, 4> d1{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
	std::array<std::byte, 4> d2{std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};


	trace.push(tagA, d1, 0);



	trace.push(tagA, d2, 4);
	{
		auto rec = trace.find_record(tagA).value();
		assert_true(rec.range.size() == 8);
		assert_true(rec.range[0] == std::byte{1});
		assert_true(rec.range[4] == std::byte{5});
	}


	std::array<std::byte, 4> d3{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
	trace.push(tagA, d3, 2);
	{
		auto rec = trace.find_record(tagA).value();

		assert_true(rec.range[1] == std::byte{2});
		assert_true(rec.range[2] == std::byte{0xFF});
		assert_true(rec.range[6] == std::byte{7});
	}


	trace.undo(tagA);
	{
		auto rec = trace.find_record(tagA).value();

		assert_true(rec.range[2] == std::byte{3});
	}





	std::array<std::byte, 2> d4{std::byte{0xAA}, std::byte{0xBB}};
	trace.push(tagA, d4, 10);
	{
		auto rec = trace.find_record(tagA).value();
		assert_true(rec.range.size() == 12);
		assert_true(rec.range[8] == std::byte{0});
		assert_true(rec.range[10] == std::byte{0xAA});
	}


	trace.undo(tagA);
	{
		auto rec = trace.find_record(tagA).value();
		assert_true(rec.range.size() == 8);
	}

	return true;
}


}
