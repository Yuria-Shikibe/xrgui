export module binary_trace;

import std;

namespace mo_yanxi{

constexpr void memcpy_constexpr(std::byte* dst, const std::byte* src, std::size_t size) noexcept{
	if consteval{
		std::ranges::copy(src, src + size, dst);
	}else{
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

		constexpr std::span<std::byte> to_span(std::byte* base) const noexcept{
			return {base + offset, size};
		}

		constexpr std::span<const std::byte> to_span(const std::byte* base) const noexcept{
			return {base + offset, size};
		}
	};

	struct entry{
		tag tag;
		unsigned target_offset;
		sub_span src_span;

		constexpr flag_type get_major() const noexcept{
			return tag.major;
		}

		constexpr flag_type get_minor() const noexcept{
			return tag.minor;
		}
	};

	struct record{
		tag tag;
		sub_span src_span;
		unsigned count;

		constexpr flag_type get_major() const noexcept{
			return tag.major;
		}

		constexpr flag_type get_minor() const noexcept{
			return tag.minor;
		}
	};

	struct export_record{
		tag tag;
		std::span<const std::byte> range;
	};

private:
	std::vector<std::byte> entry_diffs_{};
	std::vector<entry> entries_{};

	std::vector<std::byte> record_data_{};
	std::vector<record> records_{};

public:
	constexpr bool contains(tag t) const noexcept{
		//despite records is ordered, in such scale linear search should be faster
		return std::ranges::contains(records_, t, &record::tag);
	}

	[[nodiscard]] constexpr std::optional<std::span<const std::byte>> find_record(tag t) const noexcept{
		auto it = std::ranges::lower_bound(records_, t, {}, &record::tag);
		if(it != records_.end() && it->tag == t){
			return it->src_span.to_span(record_data_.data());
		}
		return std::nullopt;
	}

	constexpr void push(tag tag, std::span<const std::byte> data, unsigned offset = 0){
		unsigned currentSz = entry_diffs_.size();
		entry_diffs_.resize(entry_diffs_.size() + data.size());

		auto itr = std::ranges::lower_bound(records_, tag, {}, &record::tag);
		if(itr != records_.end() && itr->tag == tag){
			if(offset + data.size() > itr->src_span.size){
				const unsigned record_off = record_data_.size();

				record_data_.resize(record_off + data.size() + offset);
				auto subrange = itr->src_span.to_span(record_data_.data());
				memcpy_constexpr(record_data_.data() + record_off, subrange.data(), subrange.size());
				itr->src_span = {record_off, static_cast<unsigned>(data.size() + offset)};
			}

			auto subrange = itr->src_span.to_span(record_data_.data());

			for(unsigned i = 0; i < data.size(); ++i){
				entry_diffs_[i + currentSz] = subrange[i + offset] ^ data[i];
			}

			memcpy_constexpr(subrange.data() + offset, data.data(), data.size());
			++itr->count;
		}else{
			const unsigned record_off = record_data_.size();
			record_data_.resize(record_data_.size() + data.size() + offset);
			memcpy_constexpr(record_data_.data() + record_off + offset, data.data(), data.size());
			records_.insert(itr, {tag, sub_span{record_off, static_cast<unsigned>(offset + data.size())}, 1});

			memcpy_constexpr(entry_diffs_.data() + currentSz, data.data(), data.size());
		}

		entries_.emplace_back(tag, offset, sub_span{currentSz, static_cast<unsigned>(data.size())});
	}

	constexpr void undo(tag tag){
		auto rec = std::ranges::find(records_, tag, &record::tag);
		if(rec == records_.end())return;

		auto [last, _] = std::ranges::find_last(entries_, tag, &entry::tag);


		auto record = std::ranges::find(records_, tag, &record::tag);
		auto record_range = record->src_span.to_span(record_data_.data());
		auto entry_range = last->src_span.to_span(entry_diffs_.data());
		for(unsigned i = 0; i < last->src_span.size; ++i){
			record_range[i + last->target_offset] ^= entry_range[i];
		}
		entries_.erase(last);

		if(--rec->count == 0){
			records_.erase(rec);
		}
	}

	constexpr auto get_records() const noexcept{
		return records_ | std::views::transform([p = this->record_data_.data()](const record& r) constexpr -> export_record{
			return {r.tag, r.src_span.to_span(p)};
		});
	}

	constexpr bool empty() const noexcept{
		return records_.empty();
	}

	constexpr void clear() noexcept {
		records_.clear();
		entries_.clear();
		record_data_.clear();
		entry_diffs_.clear();
	}

	constexpr bool operator==(const binary_diff_trace& other) const noexcept{
		if(records_.size() != other.records_.size())return false;
		for(unsigned i = 0; i < records_.size(); ++i){
			const auto& lhs = records_[i];
			const auto& rhs = other.records_[i];
			if consteval{
				if(!std::ranges::equal(
					lhs.src_span.to_span(record_data_.data()),
					rhs.src_span.to_span(other.record_data_.data())))return false;
			}else{
				if(lhs.src_span.size != rhs.src_span.size)return false;
				if(std::memcmp(lhs.src_span.offset + record_data_.data(), rhs.src_span.offset + other.record_data_.data(), lhs.src_span.size))return false;
			}

		}
		return true;
	}
};

constexpr void assert_true(bool cond) {
    if (!cond) throw "Assertion failed";
}

consteval bool test_binary_trace() {
    binary_diff_trace trace;

    // 定义 Tag
    using Tag = binary_diff_trace::tag;
    Tag tagA{1, 0};
    Tag tagB{2, 0};

    // 模拟数据
    std::array<std::byte, 4> data1{std::byte{0xA}, std::byte{0xB}, std::byte{0xC}, std::byte{0xD}};
    std::array<std::byte, 4> data2{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};

    // Test 1: Push 新记录 (Tag A)
    // 写入 Offset 0
    trace.push(tagA, data1);

    // 验证当前记录状态
    auto records = trace.get_records();
    // 实际上 ranges::find 比较繁琐，这里简化验证逻辑
    for (const auto& rec : records) {
        if (rec.tag == tagA) {
            assert_true(std::ranges::equal(rec.range, data1));
        }
    }

    // Test 2: Push 修改 (Tag A)
    // 修改 Offset 1 处的 2 个字节: 0xB, 0xC -> 0xFF, 0xFF
    std::array<std::byte, 2> patch{std::byte{0xFF}, std::byte{0xFF}};
    trace.push(tagA, patch, 1);

    // 预期结果: 0xA, 0xFF, 0xFF, 0xD
    std::array<std::byte, 4> expected_A_modified{std::byte{0xA}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xD}};

    bool found_mod = false;
    for (const auto& rec : trace.get_records()) {
        if (rec.tag == tagA) {
            assert_true(std::ranges::equal(rec.range, expected_A_modified));
            found_mod = true;
        }
    }
    assert_true(found_mod);

    // Test 3: Undo (回滚 Tag A 的修改)
    trace.undo(tagA);

    // 预期结果回归: 0xA, 0xB, 0xC, 0xD
    bool found_orig = false;
    for (const auto& rec : trace.get_records()) {
        if (rec.tag == tagA) {
            assert_true(std::ranges::equal(rec.range, data1));
            found_orig = true;
        }
    }
    assert_true(found_orig);

    // Test 4: 扩容测试 (Resize trigger)
    // 在现有 Tag A 后追加数据，触发 record_data_ 的迁移/扩容逻辑
    // 假设 offset 4 写入 2 字节，原 size 为 4，需要 resize
    std::array<std::byte, 2> append_data{std::byte{0xEE}, std::byte{0xEE}};
    trace.push(tagA, append_data, 4);

    // 检查总长度是否变为 6
    for (const auto& rec : trace.get_records()) {
        if (rec.tag == tagA) {
            assert_true(rec.range.size() == 6);
            assert_true(rec.range[4] == std::byte{0xEE});
        }
    }

    // Test 5: 清理
    trace.clear();
    assert_true(trace.empty());

    return true;
}

// 编译期执行测试
static_assert(test_binary_trace());
}

export
template <>
struct std::hash<mo_yanxi::binary_diff_trace::tag>{
	 static std::size_t operator()(mo_yanxi::binary_diff_trace::tag tag) noexcept {
		 return std::hash<std::uint64_t>{}(std::bit_cast<std::uint64_t>(tag));
	 }
};
