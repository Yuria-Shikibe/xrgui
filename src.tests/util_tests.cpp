#include <gtest/gtest.h>

import std;

import mo_yanxi.csv;
import mo_yanxi.double_buffer;
import mo_yanxi.fixed_vector;
import mo_yanxi.unicode;
import mo_yanxi.vector_string;

namespace {

static_assert(std::ranges::random_access_range<mo_yanxi::vector_string>);
static_assert(std::ranges::sized_range<mo_yanxi::vector_string>);
static_assert(std::ranges::common_range<mo_yanxi::vector_string>);
static_assert(!std::ranges::view<mo_yanxi::vector_string>);
static_assert(!std::ranges::borrowed_range<mo_yanxi::vector_string>);
static_assert(std::same_as<std::ranges::range_reference_t<mo_yanxi::vector_string>, std::string_view>);

std::string to_string(std::u8string_view value) {
	return std::string{reinterpret_cast<const char*>(value.data()), value.size()};
}

struct parsed_cell {
	mo_yanxi::csv::coord position{};
	std::string text{};
};

struct scoped_temp_file {
	std::filesystem::path path;

	explicit scoped_temp_file(std::string_view suffix)
		: path(std::filesystem::temp_directory_path()
		       / std::format("xrgui_unit_test_{}{}", std::chrono::steady_clock::now().time_since_epoch().count(), suffix)) {
	}

	~scoped_temp_file() {
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}

	void write(std::string_view text) const {
		std::ofstream file{path, std::ios::binary};
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
		if(!file) {
			throw std::runtime_error{std::format("failed to write test file {}", path.string())};
		}
	}
};

struct counted_value {
	static inline int alive = 0;

	int value{};

	explicit counted_value(int next_value)
		: value(next_value) {
		++alive;
	}

	counted_value(const counted_value& other)
		: value(other.value) {
		++alive;
	}

	counted_value(counted_value&& other) noexcept
		: value(other.value) {
		++alive;
	}

	~counted_value() {
		--alive;
	}
};

void expect_coord(mo_yanxi::csv::coord actual, std::size_t row, std::size_t col) {
	EXPECT_EQ(row, actual.row);
	EXPECT_EQ(col, actual.col);
}

} // namespace

TEST(Csv, NumericDetectionHandlesCommonNumberForms) {
	EXPECT_TRUE(mo_yanxi::csv::is_numeric("42"));
	EXPECT_TRUE(mo_yanxi::csv::is_numeric(" \t-12.5e+3\r\n"));
	EXPECT_TRUE(mo_yanxi::csv::is_numeric("+.5"));
	EXPECT_TRUE(mo_yanxi::csv::is_numeric("1."));

	EXPECT_FALSE(mo_yanxi::csv::is_numeric(""));
	EXPECT_FALSE(mo_yanxi::csv::is_numeric("  "));
	EXPECT_FALSE(mo_yanxi::csv::is_numeric("-"));
	EXPECT_FALSE(mo_yanxi::csv::is_numeric("1e"));
	EXPECT_FALSE(mo_yanxi::csv::is_numeric("1e+"));
	EXPECT_FALSE(mo_yanxi::csv::is_numeric("1.2.3"));
	EXPECT_FALSE(mo_yanxi::csv::is_numeric("12px"));
}

TEST(Csv, ParseFilePreservesCoordinatesAndQuotedFields) {
	const scoped_temp_file csv_file{".csv"};
	csv_file.write(R"(name,quote
"Doe, Jane","hello ""world"""
empty,)");

	std::vector<parsed_cell> cells;
	mo_yanxi::csv::parse_file(
		csv_file.path,
		[&](mo_yanxi::csv::coord position, std::string_view field) {
			std::string unescaped;
			mo_yanxi::csv::unescape_csv_field(unescaped, field);
			cells.push_back(parsed_cell{.position = position, .text = std::move(unescaped)});
		});

	ASSERT_EQ(6uz, cells.size());
	expect_coord(cells[0].position, 0, 0);
	EXPECT_EQ("name", cells[0].text);
	expect_coord(cells[1].position, 0, 1);
	EXPECT_EQ("quote", cells[1].text);
	expect_coord(cells[2].position, 1, 0);
	EXPECT_EQ("Doe, Jane", cells[2].text);
	expect_coord(cells[3].position, 1, 1);
	EXPECT_EQ("hello \"world\"", cells[3].text);
	expect_coord(cells[4].position, 2, 0);
	EXPECT_EQ("empty", cells[4].text);
	expect_coord(cells[5].position, 2, 1);
	EXPECT_TRUE(cells[5].text.empty());
}

TEST(Csv, UnescapeCsvFieldOnlyCopiesWhenQuotesExist) {
	std::string target = "old";
	mo_yanxi::csv::unescape_csv_field(target, "plain");
	EXPECT_EQ("plain", target);

	mo_yanxi::csv::unescape_csv_field(target, R"(a ""quoted"" value)");
	EXPECT_EQ("a \"quoted\" value", target);
}

TEST(FixedVector, ConstructsCopiesMovesAndBoundsChecks) {
	mo_yanxi::fixed_vector<int> values(3, 7);
	EXPECT_EQ(3uz, values.size());
	EXPECT_FALSE(values.empty());
	EXPECT_EQ(7, values[0]);
	EXPECT_EQ(7, values.at(2));
	EXPECT_THROW((void)values.at(3), std::out_of_range);

	values[1] = 11;
	const mo_yanxi::fixed_vector<int> copied(values);
	EXPECT_EQ(3uz, copied.size());
	EXPECT_EQ(11, copied[1]);

	mo_yanxi::fixed_vector<int> moved(std::move(values));
	EXPECT_EQ(3uz, moved.size());
	EXPECT_EQ(0uz, values.size());
	EXPECT_EQ(11, moved[1]);

	mo_yanxi::fixed_vector<int> assigned;
	assigned = std::move(moved);
	EXPECT_EQ(3uz, assigned.size());
	EXPECT_EQ(0uz, moved.size());
	EXPECT_EQ(11, assigned[1]);
}

TEST(FixedVector, DestroysConstructedObjectsExactlyOnce) {
	counted_value::alive = 0;
	{
		mo_yanxi::fixed_vector<counted_value> values(3, counted_value{9});
		EXPECT_EQ(3, counted_value::alive);
		EXPECT_EQ(9, values[0].value);

		const mo_yanxi::fixed_vector<counted_value> copied(values);
		EXPECT_EQ(6, counted_value::alive);
		EXPECT_EQ(9, copied[2].value);
	}
	EXPECT_EQ(0, counted_value::alive);
}

TEST(DoubleBuffer, TracksCurrentAndBackupSlots) {
	mo_yanxi::double_buffer<std::vector<int>> buffer;

	buffer.get_cur().push_back(1);
	buffer.get_bak().push_back(2);

	EXPECT_EQ(0uz, buffer.current_index());
	EXPECT_EQ(1uz, buffer.backup_index());
	EXPECT_EQ((std::vector<int>{1}), buffer.current());
	EXPECT_EQ((std::vector<int>{2}), buffer.backup());

	buffer.swap();

	EXPECT_EQ(1uz, buffer.current_index());
	EXPECT_EQ(0uz, buffer.backup_index());
	EXPECT_EQ((std::vector<int>{2}), buffer.get_cur());
	EXPECT_EQ((std::vector<int>{1}), buffer.get_bak());
}

TEST(DoubleBuffer, ConstructsAndClearsBothBuffers) {
	mo_yanxi::double_buffer<std::string> buffer(3, 'x');

	EXPECT_EQ("xxx", buffer.current());
	EXPECT_EQ("xxx", buffer.backup());

	buffer.backup() = "yyy";
	buffer.swap_buffers();
	EXPECT_EQ("yyy", buffer.current());
	EXPECT_EQ("xxx", buffer.backup());

	buffer.clear();
	EXPECT_TRUE(buffer.current().empty());
	EXPECT_TRUE(buffer.backup().empty());
}

TEST(DoubleBuffer, SupportsMoveOnlyBufferValues) {
	auto buffer = mo_yanxi::double_buffer<std::unique_ptr<int>>::from_buffers(
		std::make_unique<int>(7),
		std::make_unique<int>(9)
	);

	ASSERT_TRUE(buffer.current());
	ASSERT_TRUE(buffer.backup());
	EXPECT_EQ(7, *buffer.current());
	EXPECT_EQ(9, *buffer.backup());

	buffer.flip();

	ASSERT_TRUE(buffer.current());
	ASSERT_TRUE(buffer.backup());
	EXPECT_EQ(9, *buffer.get_cur());
	EXPECT_EQ(7, *buffer.get_bak());
}

TEST(VectorString, StoresStringsSeparatedByZero) {
	mo_yanxi::vector_string values;
	EXPECT_EQ(0uz, values.storage_size());
	EXPECT_TRUE(values.indices().empty());

	EXPECT_EQ(std::string_view{"alpha"}, values.push_back("alpha"));
	EXPECT_EQ(std::string_view{"xxx"}, values.emplace_back(3, 'x'));
	values.push_back("");

	EXPECT_EQ(3uz, values.size());
	EXPECT_EQ(11uz, values.storage_size());
	EXPECT_EQ(std::string("alpha\0xxx\0\0", 11), values.storage());
	EXPECT_TRUE(std::ranges::equal(values.indices(), std::array{5uz, 9uz, 10uz}));
	EXPECT_EQ(10uz, values.indices().back());
	EXPECT_EQ(values.storage_size() - 1, values.indices().back());
	EXPECT_EQ("alpha", values[0]);
	EXPECT_EQ("xxx", values.at(1));
	EXPECT_TRUE(values[2].empty());
	EXPECT_EQ(5uz, values.span_at(0).size());

	auto iter = values.begin();
	static_assert(std::random_access_iterator<decltype(iter)>);
	EXPECT_EQ("xxx", iter[1]);
	EXPECT_EQ("", *(iter + 2));
	EXPECT_EQ(3, values.end() - values.begin());
	EXPECT_EQ("xxx", *(values.end() - 2));
	EXPECT_EQ("", *(values.end() - 1));

	std::vector<std::string> copied;
	for(std::string_view value : values) {
		copied.emplace_back(value);
	}
	EXPECT_EQ((std::vector<std::string>{"alpha", "xxx", ""}), copied);
}

TEST(VectorString, AllowsOnlyOpenTailModification) {
	mo_yanxi::vector_string values;
	values.push_back("sealed");

	const auto index = values.modify_begin(9);
#ifndef NDEBUG
	EXPECT_TRUE(values.modifying());
#endif
	EXPECT_EQ(2uz, values.size());

	auto span = values.span_at(index);
	ASSERT_EQ(9uz, span.size());
	std::char_traits<char>::copy(span.data(), "work_item", span.size());
	EXPECT_EQ("work_item", values[index]);
	span[4] = '-';

	const auto inserted = values.modify_end(index);
#ifndef NDEBUG
	EXPECT_FALSE(values.modifying());
#endif
	EXPECT_EQ("work-item", inserted);
	EXPECT_EQ(2uz, values.size());
	EXPECT_EQ("sealed", values[0]);
	EXPECT_EQ("work-item", values[1]);
	EXPECT_TRUE(std::ranges::equal(values.indices(), std::array{6uz, 16uz}));
	EXPECT_EQ(16uz, values.indices().back());
	EXPECT_EQ(values.storage_size() - 1, values.indices().back());
#ifndef NDEBUG
	EXPECT_THROW((void)values.modify_end(index), std::logic_error);
#endif
}

TEST(VectorString, RejectsEmbeddedSeparatorsAndNestedModification) {
	mo_yanxi::vector_string values;

	EXPECT_THROW((void)values.push_back(std::string_view{"a\0b", 3}), std::invalid_argument);

	const auto index = values.modify_begin(5);
#ifndef NDEBUG
	EXPECT_THROW((void)values.modify_begin(), std::logic_error);
#endif

	auto span = values.span_at(index);
	std::char_traits<char>::copy(span.data(), "a\0bcd", span.size());
	EXPECT_THROW((void)values.modify_end(index), std::invalid_argument);

	std::char_traits<char>::copy(span.data(), "valid", span.size());
	EXPECT_EQ("valid", values.modify_end(index));
#ifndef NDEBUG
	EXPECT_THROW((void)values.modify_end(index), std::logic_error);
#endif
}

TEST(Unicode, ConvertsUtf8Utf16AndUtf32RoundTrips) {
	const std::u8string_view utf8 = u8"Hello, \u4e16\u754c \U0001f30d";
	const std::u32string expected_utf32 = U"Hello, \u4e16\u754c \U0001f30d";

	const auto as_utf32 = mo_yanxi::unicode::utf8_to_utf32(utf8);
	EXPECT_EQ(expected_utf32, as_utf32);

	const auto round_tripped = mo_yanxi::unicode::utf32_to_utf8<char>(as_utf32);
	EXPECT_EQ(to_string(utf8), round_tripped);

	const std::u16string_view utf16 = u"A\U0001f600";
	EXPECT_EQ(2uz, mo_yanxi::unicode::utf32_length_from_utf16(utf16));
	EXPECT_EQ(std::u32string{U"A\U0001f600"}, mo_yanxi::unicode::utf16_to_utf32(utf16));
}

TEST(Unicode, AppendConversionsPreserveExistingContent) {
	std::u32string utf32 = U"prefix:";
	mo_yanxi::unicode::append_utf8_to_utf32(std::u8string_view{u8"\u03b1"}, utf32);
	EXPECT_EQ(std::u32string{U"prefix:\u03b1"}, utf32);

	std::string utf8 = "id=";
	mo_yanxi::unicode::append_utf32_to_utf8(std::u32string_view{U"\U0001f600"}, utf8);
	EXPECT_EQ(to_string(u8"id=\U0001f600"), utf8);
}
