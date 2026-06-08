#include <gtest/gtest.h>

import std;

import mo_yanxi.csv;
import mo_yanxi.fixed_vector;
import mo_yanxi.unicode;

namespace {

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
