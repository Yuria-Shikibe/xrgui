export module binary_trace;

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
		unsigned offset; // record_data_ 中的物理偏移
		unsigned size; // 物理大小

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
		unsigned offset; // 逻辑起始偏移
		std::span<const std::byte> range;
	};

	struct record{
		tag tag;
		sub_span src_span; // 物理存储位置
		unsigned logical_offset; // 逻辑起始偏移
		unsigned count; // 操作计数引用
	};

private:
	enum class op_type : std::uint8_t{
		new_record, // 创建了新记录
		append, // 物理尾部直接追加 (零拷贝)
		diff, // 原地异或修改
		realloc // 发生了重分配 (Copy-on-Write)
	};

	struct entry{
		tag tag;
		op_type type;

		// 联合存储不同操作所需的回滚信息，节省空间
		// Diff 模式: diff_span 存储 XOR 数据位置, target_rel_offset 存储修改位置
		// Append 模式: target_rel_offset 存储追加的长度 (用于 resize 回滚)
		// Realloc 模式: old_span 存储旧物理位置, old_logical_offset 存储旧逻辑偏移
		sub_span data_span;
		unsigned aux_val;
	};

	std::vector<std::byte> entry_diffs_{}; // 仅存储 Diff 操作的 XOR 数据
	std::vector<entry> entries_{};

	std::vector<std::byte> record_data_{}; // 存储主要数据
	std::vector<record> records_{};

	// 内部辅助：线性查找 (小数据量下比二分快且对 cache 友好)
	[[nodiscard]] constexpr auto find_record_iter(tag t) noexcept{
		return std::ranges::find(records_, t, &record::tag);
	}

	[[nodiscard]] constexpr auto find_record_iter(tag t) const noexcept{
		return std::ranges::find(records_, t, &record::tag);
	}

public:
	// 预留空间，避免动态扩容开销
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

		// Case 1: 新记录
		if(itr == records_.end()){
			const unsigned phys_off = static_cast<unsigned>(record_data_.size());
			// 直接追加到主 buffer
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

		// 计算合并后的逻辑范围
		const unsigned new_start = std::min(req_start, cur_start);
		const unsigned new_end = std::max(req_end, cur_end);

		// Case 2: 尾部追加优化 (Tail Extension)
		// 条件：逻辑上是追加，且物理上该记录正好位于 record_data_ 的末尾
		// 收益：无需分配新 block，无需拷贝旧数据
		bool is_logical_append = (req_start == cur_end); // 紧接逻辑尾部
		bool is_phys_tail = (rec.src_span.offset + rec.src_span.size == record_data_.size());

		if(is_logical_append && is_phys_tail){
			unsigned append_size = data.size();
			unsigned old_phys_size = static_cast<unsigned>(record_data_.size());

			record_data_.resize(old_phys_size + append_size);
			memcpy_constexpr(record_data_.data() + old_phys_size, data.data(), append_size);

			// 更新记录
			rec.src_span.size += append_size;
			rec.count++;

			// 记录历史: aux_val 存追加长度
			entries_.emplace_back(tag, op_type::append, sub_span{0, 0}, append_size);
			return;
		}

		// Case 3: 原地修改 (In-Place Diff)
		// 条件：新数据完全落在现有逻辑范围内
		if(req_start >= cur_start && req_end <= cur_end){
			unsigned rel_offset = req_start - cur_start;
			unsigned diff_off = static_cast<unsigned>(entry_diffs_.size());

			entry_diffs_.resize(diff_off + data.size());
			auto phys_span = rec.src_span.to_span(record_data_.data());

			// 计算 XOR 并写入 diff buffer
			for(size_t i = 0; i < data.size(); ++i){
				entry_diffs_[diff_off + i] = phys_span[rel_offset + i] ^ data[i];
			}
			// 应用新数据
			memcpy_constexpr(phys_span.data() + rel_offset, data.data(), data.size());

			rec.count++;
			// 记录历史: data_span 存 diff 位置, aux_val 存相对偏移
			entries_.emplace_back(tag, op_type::diff, sub_span{diff_off, static_cast<unsigned>(data.size())},
				rel_offset);
			return;
		}

		// Case 4: 重分配 (Realloc / Copy-on-Write)
		// 场景：头部扩展、中间填补 Gap、或非尾部的追加
		{
			unsigned new_size = new_end - new_start;
			unsigned new_phys_off = static_cast<unsigned>(record_data_.size());

			// 记录旧状态以便 Undo
			entries_.emplace_back(tag, op_type::realloc, rec.src_span, rec.logical_offset);

			record_data_.resize(new_phys_off + new_size); // 扩展空间 (默认值0处理 gap)
			std::byte* base = record_data_.data();

			// 1. 搬运旧数据
			if(rec.src_span.size > 0){
				unsigned old_rel = cur_start - new_start;
				memcpy_constexpr(base + new_phys_off + old_rel, base + rec.src_span.offset, rec.src_span.size);
			}

			// 2. 写入新数据 (覆盖)
			unsigned new_rel = req_start - new_start;
			memcpy_constexpr(base + new_phys_off + new_rel, data.data(), data.size());

			// 更新记录
			rec.src_span = {new_phys_off, new_size};
			rec.logical_offset = new_start;
			rec.count++;
		}
	}

	constexpr void undo(tag tag){
		auto rec_it = find_record_iter(tag);
		if(rec_it == records_.end()) return;

		// 查找该 tag 的最后一条 entry
		// 优化：从后往前扫，找到匹配 tag 即停
		auto entry_it = std::ranges::find(entries_ | std::views::reverse, tag, &entry::tag);
		if(entry_it == (entries_ | std::views::reverse).end()) return;

		// 获取正向 iterator (base() 返回 reverse_iterator 对应的下一个位置，所以要减 1)
		auto forward_it = std::prev(entry_it.base());
		const auto& last_entry = *forward_it;
		record& rec = *rec_it;

		switch(last_entry.type){
		case op_type::new_record :{
			// 逻辑：如果是新记录，undo 意味着彻底删除
			// 物理空间无法轻易回收 (record_data_ append only)，但逻辑记录移除
			records_.erase(rec_it);
			break;
		}
		case op_type::append :{
			// 逻辑：直接缩减 size
			unsigned appended_size = last_entry.aux_val;
			rec.src_span.size -= appended_size;
			rec.count--;
			// 物理空间优化：如果正好在物理末尾，可以真正释放 vector 空间
			if(rec.src_span.offset + rec.src_span.size + appended_size == record_data_.size()){
				record_data_.resize(record_data_.size() - appended_size);
			}
			break;
		}
		case op_type::diff :{
			// 逻辑：XOR 还原
			auto diff_range = last_entry.data_span.to_span(entry_diffs_.data());
			auto rec_range = rec.src_span.to_span(record_data_.data());
			unsigned rel_offset = last_entry.aux_val;

			for(unsigned i = 0; i < diff_range.size(); ++i){
				rec_range[rel_offset + i] ^= diff_range[i];
			}

			// 清理 diff buffer (如果位于末尾)
			if(last_entry.data_span.offset + last_entry.data_span.size == entry_diffs_.size()){
				entry_diffs_.resize(last_entry.data_span.offset);
			}
			rec.count--;
			break;
		}
		case op_type::realloc :{
			// 逻辑：指针指回旧位置
			rec.src_span = last_entry.data_span; // old_span
			rec.logical_offset = last_entry.aux_val; // old_logical_offset
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

	// 新增：用于 load_tag 和 store_tag 暂存拷贝数据的缓冲区
	std::vector<std::byte> buffer_{};

	[[nodiscard]] constexpr auto find_record_iter(tag t) noexcept{
		return std::ranges::find(records_, t, &record::tag);
	}

	[[nodiscard]] constexpr auto find_record_iter(tag t) const noexcept{
		return std::ranges::find(records_, t, &record::tag);
	}

public:
	// 预留空间
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
			return {}; // 没找到则返回空
		}

		const record& rec = *it;
		const unsigned copy_size = rec.src_span.size;
		const unsigned buffer_offset = static_cast<unsigned>(buffer_.size());

		// 在 buffer_ 尾部开辟新空间
		buffer_.resize(buffer_offset + copy_size);

		// 将 record_data_ 中的数据拷贝到 buffer_
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

		// ==========================================
		// 阶段 1：计算阶段 (无副作用，不修改任何状态)
		// ==========================================
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

		// ==========================================
		// 阶段 2：可能抛出异常的阶段 (潜在的 std::bad_alloc)
		// ==========================================
		if(requires_resize){
			// std::vector::resize 对于 trivial type (如 std::byte) 提供强异常保证。
			// 如果在此处由于内存不足抛出异常，此函数将直接中止，
			// 而 records_、record_data_ 和 buffer_ 的状态尚未发生任何改变。
			record_data_.resize(new_total_size);
		}

		// ==========================================
		// 阶段 3：提交阶段 (Commit Phase，保证 noexcept)
		// ==========================================
		// 运行到这里说明不再有抛出异常的可能，可以安全地覆写内部状态
		record& rec = *it;
		rec.logical_offset = r.logical_offset;
		auto buffer_span = r.data.to_span(buffer_.data());

		if(is_shrink_or_same){
			auto target_span = rec.src_span.to_span(record_data_.data());
			memcpy_constexpr(target_span.data(), buffer_span.data(), r.data.size);
			rec.src_span.size = r.data.size;
		} else{
			// new_phys_off 已经在阶段 1 计算完毕，直接写入
			memcpy_constexpr(record_data_.data() + new_phys_off, buffer_span.data(), r.data.size);
			rec.src_span = {new_phys_off, r.data.size};
		}

		// 4. 清理 buffer_
		// vector 缩容 (缩小 size) 绝不会抛出异常；内存清零 (memset/fill) 也绝不会抛出异常。
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

		// Case 1: 新记录
		if(itr == records_.end()){
			const unsigned phys_off = static_cast<unsigned>(record_data_.size());

			// 直接追加到主 buffer
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

		// 计算合并后的逻辑范围
		const unsigned new_start = std::min(req_start, cur_start);
		const unsigned new_end = std::max(req_end, cur_end);

		// Case 2: 尾部追加优化 (Tail Extension)
		bool is_logical_append = (req_start == cur_end);
		bool is_phys_tail = (rec.src_span.offset + rec.src_span.size == record_data_.size());

		if(is_logical_append && is_phys_tail){
			unsigned append_size = data.size();
			unsigned old_phys_size = static_cast<unsigned>(record_data_.size());

			record_data_.resize(old_phys_size + append_size);
			memcpy_constexpr(record_data_.data() + old_phys_size, data.data(), append_size);

			// 更新记录
			rec.src_span.size += append_size;
			rec.count++;
			return;
		}

		// Case 3: 原地修改 (In-Place Overwrite)
		// 条件：新数据完全落在现有逻辑范围内。直接覆盖物理内存，无需记录 diff。
		if(req_start >= cur_start && req_end <= cur_end){
			unsigned rel_offset = req_start - cur_start;
			auto phys_span = rec.src_span.to_span(record_data_.data());

			// 直接应用新数据
			memcpy_constexpr(phys_span.data() + rel_offset, data.data(), data.size());
			rec.count++;
			return;
		}

		// Case 4: 重分配 (Realloc / Copy-on-Write)
		// 场景：头部扩展、中间填补 Gap、或非尾部的追加
		{
			unsigned new_size = new_end - new_start;
			unsigned new_phys_off = static_cast<unsigned>(record_data_.size());

			record_data_.resize(new_phys_off + new_size); // 扩展空间 (默认值0处理 gap)
			std::byte* base = record_data_.data();

			// 1. 搬运旧数据
			if(rec.src_span.size > 0){
				unsigned old_rel = cur_start - new_start;
				memcpy_constexpr(base + new_phys_off + old_rel, base + rec.src_span.offset, rec.src_span.size);
			}

			// 2. 写入新数据 (覆盖)
			unsigned new_rel = req_start - new_start;
			memcpy_constexpr(base + new_phys_off + new_rel, data.data(), data.size());

			// 更新记录
			rec.src_span = {new_phys_off, new_size};
			rec.logical_offset = new_start;
			rec.count++;
		}
	}


	constexpr bool empty() const noexcept{
		return records_.empty();
	}

	// ---------- 辅助功能 (可根据需要保留原版 push 逻辑来初始化数据) ----------

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

// ---------------------- 测试代码 ----------------------
constexpr void assert_true(bool cond){
	if(!cond) throw "Assertion failed";
}

consteval bool test_optimized_trace(){
	binary_diff_trace trace;
	using Tag = binary_diff_trace::tag;
	Tag tagA{1, 1};

	std::array<std::byte, 4> d1{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
	std::array<std::byte, 4> d2{std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};

	// 1. New Record [0, 4) -> Value: {1,2,3,4}
	trace.push(tagA, d1, 0);

	// 2. Tail Append [4, 8) -> Value: {1,2,3,4, 5,6,7,8}
	// 这应该触发 Append 优化，不发生 copy
	trace.push(tagA, d2, 4);
	{
		auto rec = trace.find_record(tagA).value();
		assert_true(rec.range.size() == 8);
		assert_true(rec.range[0] == std::byte{1});
		assert_true(rec.range[4] == std::byte{5});
	}

	// 3. In-Place Diff [2, 6) -> 覆盖中间
	std::array<std::byte, 4> d3{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
	trace.push(tagA, d3, 2);
	{
		auto rec = trace.find_record(tagA).value();
		// {1, 2, FF, FF, FF, FF, 7, 8}
		assert_true(rec.range[1] == std::byte{2});
		assert_true(rec.range[2] == std::byte{0xFF});
		assert_true(rec.range[6] == std::byte{7});
	}

	// 4. Undo Diff
	trace.undo(tagA);
	{
		auto rec = trace.find_record(tagA).value();
		// Back to {1,2,3,4, 5,6,7,8}
		assert_true(rec.range[2] == std::byte{3});
	}

	// 5. Realloc (Prepend) [-4, 8) -> Offset changes to -4 (uint wrap behavior if strictly unsigned, but logically it extends left)
	// 这里演示 Gap Fill / Prepend
	// 假设我们在 offset 0 之前插入（逻辑不支持负数，但支持向前扩展如果 current > 0）
	// 让我们做 Gap fill: 之前是 [0, 8)，现在 push [10, 12)
	std::array<std::byte, 2> d4{std::byte{0xAA}, std::byte{0xBB}};
	trace.push(tagA, d4, 10);
	{
		auto rec = trace.find_record(tagA).value();
		assert_true(rec.range.size() == 12); // 0..12
		assert_true(rec.range[8] == std::byte{0}); // Gap
		assert_true(rec.range[10] == std::byte{0xAA});
	}

	// 6. Undo Realloc
	trace.undo(tagA);
	{
		auto rec = trace.find_record(tagA).value();
		assert_true(rec.range.size() == 8);
	}

	return true;
}

// static_assert(test_optimized_trace());
}
