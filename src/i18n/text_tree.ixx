export module mo_yanxi.text_tree;

import std;
import magic_enum;
export import mo_yanxi.referenced_ptr;

namespace mo_yanxi::text {

export using node_id = std::uint32_t;
export using namespace_id = node_id;

export inline constexpr node_id invalid_node_id = std::numeric_limits<node_id>::max();

export class frozen_text_tree;
export struct frozen_text_tree_deleter {
	void operator()(frozen_text_tree* tree) const noexcept;
};
export class text_tree_cursor;
export class text_tree_builder;

export using frozen_text_tree_ptr = mo_yanxi::referenced_ptr<frozen_text_tree, frozen_text_tree_deleter>;

export enum class node_kind : std::uint8_t {
	directory,
	text,
	tree_pointer,
};

export enum class lookup_status : std::uint8_t {
	ok,
	missing,
	invalid_path,
	not_namespace,
	too_deep,
	symlink_cycle,
	tree_pointer_cycle,
};

export struct lookup_result {
	lookup_status status{lookup_status::missing};
	node_kind kind{node_kind::directory};
	const frozen_text_tree* tree{};
	node_id node{invalid_node_id};
	std::string_view text{};

	[[nodiscard]] explicit operator bool() const noexcept {
		return status == lookup_status::ok;
	}

	[[nodiscard]] bool is_text() const noexcept {
		return status == lookup_status::ok && kind == node_kind::text;
	}
};

namespace detail {

enum class edge_kind : std::uint8_t {
	direct,
	symbolic_link,
};

struct parsed_segment {
	std::string_view value{};
	std::string_view remain{};
	bool valid{};
};

[[nodiscard]] constexpr bool is_segment_char(const char c) noexcept {
	const auto uc = static_cast<unsigned char>(c);
	return (uc >= 'a' && uc <= 'z')
		|| (uc >= 'A' && uc <= 'Z')
		|| (uc >= '0' && uc <= '9')
		|| uc == '_'
		|| uc == '-';
}

[[nodiscard]] constexpr bool valid_segment(std::string_view segment) noexcept {
	if(segment.empty()) {
		return false;
	}
	return std::ranges::all_of(segment, is_segment_char);
}

[[nodiscard]] constexpr parsed_segment next_segment(std::string_view path) noexcept {
	if(path.empty()) {
		return {};
	}
	const auto dot = path.find('.');
	const auto segment = dot == std::string_view::npos ? path : path.substr(0, dot);
	if(!valid_segment(segment)) {
		return {};
	}
	if(dot == std::string_view::npos) {
		return {.value = segment, .remain = {}, .valid = true};
	}
	const auto remain = path.substr(dot + 1);
	if(remain.empty()) {
		return {};
	}
	return {.value = segment, .remain = remain, .valid = true};
}

[[nodiscard]] constexpr bool valid_dotted_path(std::string_view path) noexcept {
	if(path.empty()) {
		return false;
	}
	while(!path.empty()) {
		const auto segment = next_segment(path);
		if(!segment.valid) {
			return false;
		}
		path = segment.remain;
	}
	return true;
}

[[nodiscard]] constexpr std::uint64_t ascii_hash64(std::string_view text) noexcept {
	std::uint64_t value = 14695981039346656037ull;
	for(const char c : text) {
		value ^= static_cast<unsigned char>(c);
		value *= 1099511628211ull;
	}
	return value;
}

[[nodiscard]] constexpr bool is_local_namespace(node_kind kind) noexcept {
	return kind == node_kind::directory;
}

[[nodiscard]] constexpr std::string_view status_name(lookup_status status) noexcept {
	const std::string_view name = ::magic_enum::enum_name(status);
	return name.empty() ? std::string_view{"unknown"} : name;
}

struct edge_record {
	std::uint64_t hash{};
	std::uint32_t name_offset{};
	std::uint32_t name_size{};
	edge_kind kind{edge_kind::direct};
	node_id target{invalid_node_id};
	std::uint32_t link_offset{};
	std::uint32_t link_size{};
};

struct node_record {
	node_kind kind{node_kind::directory};
	std::uint32_t edge_begin{};
	std::uint32_t edge_count{};
	std::uint32_t text_offset{};
	std::uint32_t text_size{};
	std::uint32_t tree_ptr_index{invalid_node_id};
};

struct build_edge {
	edge_kind kind{edge_kind::direct};
	node_id target{invalid_node_id};
	std::string link_target{};
};

struct transparent_string_hash {
	using is_transparent = void;

	[[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
		return std::hash<std::string_view>{}(value);
	}
};

struct transparent_string_equal {
	using is_transparent = void;

	[[nodiscard]] bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
		return lhs == rhs;
	}
};

struct build_node {
	node_kind kind{node_kind::directory};
	std::unordered_map<std::string, build_edge, transparent_string_hash, transparent_string_equal> children{};
	std::string text{};
	frozen_text_tree_ptr tree{};
	node_id canonical_parent{invalid_node_id};
	std::string canonical_name{};
};

[[nodiscard]] constexpr std::size_t align_up(std::size_t value, std::size_t alignment) noexcept {
	return (value + alignment - 1) / alignment * alignment;
}

}


export class frozen_text_tree : public mo_yanxi::referenced_object_atomic {
public:
	static constexpr std::size_t max_path_depth = 64;
	static constexpr std::size_t max_symlink_hops = 32;
	static constexpr std::size_t max_tree_pointer_hops = 32;

	frozen_text_tree() = delete;
	frozen_text_tree(const frozen_text_tree&) = delete;
	frozen_text_tree& operator=(const frozen_text_tree&) = delete;
	frozen_text_tree(frozen_text_tree&&) = delete;
	frozen_text_tree& operator=(frozen_text_tree&&) = delete;

	[[nodiscard]] lookup_result lookup(std::string_view path) const noexcept {
		if(nodes_.empty() || path.empty()) {
			return {.status = path.empty() ? lookup_status::invalid_path : lookup_status::missing};
		}
		resolver_state state{};
		state.depth = 1;
		state.stack[0] = path_frame{this, root_};
		return lookup_from(this, root_, path, state);
	}

	[[nodiscard]] std::optional<std::string_view> find_text(std::string_view path) const noexcept {
		const auto result = lookup(path);
		if(result.is_text()) {
			return result.text;
		}
		return std::nullopt;
	}

	[[nodiscard]] text_tree_cursor operator[](std::string_view path) const noexcept;

	[[nodiscard]] const std::byte* raw_data() const noexcept {
		return payload_data();
	}

	[[nodiscard]] std::size_t raw_size() const noexcept {
		return buffer_size_;
	}

	[[nodiscard]] bool empty() const noexcept {
		return nodes_.empty();
	}

	[[nodiscard]] std::size_t reference_count() const noexcept {
		return this->ref_count();
	}

	[[nodiscard]] node_kind kind_of(node_id id) const {
		check_node_id(id, nodes_.size());
		return nodes_[id].kind;
	}

private:
	friend class text_tree_builder;
	friend class text_tree_cursor;
	friend struct frozen_text_tree_deleter;

	struct payload_layout {
		std::size_t nodes_offset{};
		std::size_t edges_offset{};
		std::size_t ptrs_offset{};
		std::size_t strings_offset{};
		std::size_t total_size{};
	};

	struct path_frame {
		const frozen_text_tree* tree{};
		node_id node{invalid_node_id};
	};

	struct resolver_state {
		std::array<path_frame, max_path_depth> stack{};
		std::size_t depth{};
		std::size_t symlink_hops{};
		std::size_t tree_pointer_hops{};
	};

	std::size_t buffer_size_{};
	std::span<const detail::node_record> nodes_{};
	std::span<const detail::edge_record> edges_{};
	std::span<const frozen_text_tree_ptr> tree_ptrs_{};
	const char* strings_{};
	std::size_t strings_size_{};
	node_id root_{};

	explicit frozen_text_tree(std::size_t buffer_size, node_id root) noexcept
		: buffer_size_{buffer_size},
		  root_{root} {
	}

	~frozen_text_tree() {
		destroy_tree_ptrs();
	}

	[[nodiscard]] static constexpr std::size_t allocation_alignment() noexcept {
		return std::max(alignof(frozen_text_tree), alignof(std::max_align_t));
	}

	[[nodiscard]] static constexpr std::size_t payload_offset() noexcept {
		return detail::align_up(sizeof(frozen_text_tree), alignof(std::max_align_t));
	}

	[[nodiscard]] std::byte* payload_data() noexcept {
		return reinterpret_cast<std::byte*>(this) + payload_offset();
	}

	[[nodiscard]] const std::byte* payload_data() const noexcept {
		return reinterpret_cast<const std::byte*>(this) + payload_offset();
	}

	[[nodiscard]] static payload_layout make_payload_layout(
		std::size_t node_count,
		std::size_t edge_count,
		std::size_t tree_ptr_count,
		std::size_t string_size) noexcept {
		const auto nodes_offset = std::size_t{};
		const auto edges_offset = detail::align_up(node_count * sizeof(detail::node_record), alignof(detail::edge_record));
		const auto ptrs_offset = detail::align_up(edges_offset + edge_count * sizeof(detail::edge_record), alignof(frozen_text_tree_ptr));
		const auto strings_offset = detail::align_up(ptrs_offset + tree_ptr_count * sizeof(frozen_text_tree_ptr), alignof(char));
		return payload_layout{
			.nodes_offset = nodes_offset,
			.edges_offset = edges_offset,
			.ptrs_offset = ptrs_offset,
			.strings_offset = strings_offset,
			.total_size = strings_offset + string_size,
		};
	}

	static void check_node_id(node_id id, std::size_t size) {
		if(id >= size) {
			throw std::out_of_range{"text tree node id out of range"};
		}
	}

	void destroy_tree_ptrs() noexcept {
		if(!tree_ptrs_.empty()) {
			for(const auto& ptr : tree_ptrs_) {
				std::destroy_at(std::addressof(const_cast<frozen_text_tree_ptr&>(ptr)));
			}
		}
		tree_ptrs_ = {};
	}

	[[nodiscard]] std::string_view string_at(std::uint32_t offset, std::uint32_t size) const noexcept {
		return std::string_view{strings_ + offset, size};
	}

	[[nodiscard]] std::string_view name_of(const detail::edge_record& edge) const noexcept {
		return string_at(edge.name_offset, edge.name_size);
	}

	[[nodiscard]] std::string_view link_of(const detail::edge_record& edge) const noexcept {
		return string_at(edge.link_offset, edge.link_size);
	}

	[[nodiscard]] const detail::edge_record* find_edge(node_id id, std::string_view segment) const noexcept {
		if(id >= nodes_.size()) {
			return nullptr;
		}
		const auto& node = nodes_[id];
		if(!detail::is_local_namespace(node.kind)) {
			return nullptr;
		}

		const auto first = edges_.begin() + node.edge_begin;
		const auto last = first + node.edge_count;
		if(node.edge_count <= 8) {
			const auto found = std::ranges::find_if(first, last, [&](const detail::edge_record& edge) {
				return name_of(edge) == segment;
			});
			return found == last ? nullptr : std::addressof(*found);
		}

		const auto hash = detail::ascii_hash64(segment);
		auto found = std::ranges::lower_bound(first, last, hash, {}, &detail::edge_record::hash);
		for(; found != last && found->hash == hash; ++found) {
			if(name_of(*found) == segment) {
				return std::addressof(*found);
			}
		}
		return nullptr;
	}

	[[nodiscard]] lookup_result result_for(const frozen_text_tree* tree, node_id id) const noexcept {
		const auto& node = tree->nodes_[id];
		lookup_result result{.status = lookup_status::ok, .kind = node.kind, .tree = tree, .node = id};
		if(node.kind == node_kind::text) {
			result.text = tree->string_at(node.text_offset, node.text_size);
		}
		return result;
	}

	[[nodiscard]] bool push_frame(resolver_state& state, const frozen_text_tree* tree, node_id id) const noexcept {
		if(state.depth >= state.stack.size()) {
			return false;
		}
		state.stack[state.depth++] = path_frame{tree, id};
		return true;
	}

	[[nodiscard]] lookup_result enter_tree_pointer_if_needed(
		const frozen_text_tree*& tree,
		node_id& current,
		resolver_state& state) const noexcept {
		while(tree->nodes_[current].kind == node_kind::tree_pointer) {
			if(++state.tree_pointer_hops > max_tree_pointer_hops) {
				return {.status = lookup_status::tree_pointer_cycle};
			}
			const auto index = tree->nodes_[current].tree_ptr_index;
			if(index >= tree->tree_ptrs_.size() || !tree->tree_ptrs_[index]) {
				return {.status = lookup_status::missing};
			}
			tree = tree->tree_ptrs_[index].get();
			current = tree->root_;
			if(!push_frame(state, tree, current)) {
				return {.status = lookup_status::too_deep};
			}
		}
		return result_for(tree, current);
	}

	[[nodiscard]] lookup_result lookup_from(
		const frozen_text_tree* tree,
		node_id current,
		std::string_view path,
		resolver_state& state) const noexcept {
		if(path.empty()) {
			return {.status = lookup_status::invalid_path};
		}

		if(auto entered = enter_tree_pointer_if_needed(tree, current, state); !entered) {
			return entered;
		}

		while(true) {
			const auto segment = detail::next_segment(path);
			if(!segment.valid) {
				return {.status = lookup_status::invalid_path};
			}

			const auto* edge = tree->find_edge(current, segment.value);
			if(edge == nullptr) {
				return {.status = lookup_status::missing};
			}

			if(edge->kind == detail::edge_kind::symbolic_link) {
				if(++state.symlink_hops > max_symlink_hops) {
					return {.status = lookup_status::symlink_cycle};
				}
				auto target = resolve_link_target(tree, current, tree->link_of(*edge), state);
				if(!target) {
					return target;
				}
				tree = target.tree;
				current = target.node;
			} else {
				current = edge->target;
				if(!push_frame(state, tree, current)) {
					return {.status = lookup_status::too_deep};
				}
			}

			if(segment.remain.empty()) {
				return result_for(tree, current);
			}

			if(auto entered = enter_tree_pointer_if_needed(tree, current, state); !entered) {
				return entered;
			}
			if(!detail::is_local_namespace(tree->nodes_[current].kind)) {
				return {.status = lookup_status::not_namespace, .kind = tree->nodes_[current].kind, .tree = tree, .node = current};
			}
			path = segment.remain;
		}
	}

	[[nodiscard]] lookup_result resolve_link_target(
		const frozen_text_tree* containing_tree,
		node_id containing_node,
		std::string_view target,
		resolver_state& state) const noexcept {
		if(target.empty()) {
			return {.status = lookup_status::invalid_path};
		}

		const frozen_text_tree* tree = containing_tree;
		node_id current = containing_node;
		std::string_view path = target;

		if(path.front() == '/') {
			path.remove_prefix(1);
			current = tree->root_;
			state.depth = 1;
			state.stack[0] = path_frame{tree, current};
		} else {
			while(true) {
				if(path == ".") {
					return result_for(tree, current);
				}
				if(path == "..") {
					if(state.depth <= 1) {
						return {.status = lookup_status::missing};
					}
					--state.depth;
					tree = state.stack[state.depth - 1].tree;
					current = state.stack[state.depth - 1].node;
					return result_for(tree, current);
				}
				if(path.starts_with("./")) {
					path.remove_prefix(2);
					continue;
				}
				if(path.starts_with("../")) {
					if(state.depth <= 1) {
						return {.status = lookup_status::missing};
					}
					--state.depth;
					tree = state.stack[state.depth - 1].tree;
					current = state.stack[state.depth - 1].node;
					path.remove_prefix(3);
					continue;
				}
				break;
			}
		}

		if(path.empty()) {
			return result_for(tree, current);
		}
		return lookup_from(tree, current, path, state);
	}

	[[nodiscard]] lookup_result lookup_from_node(
		const frozen_text_tree* tree,
		node_id current,
		std::string_view path) const noexcept {
		if(tree == nullptr || current == invalid_node_id) {
			return {.status = lookup_status::missing};
		}
		if(path.empty()) {
			return {.status = lookup_status::invalid_path};
		}
		resolver_state state{};
		state.depth = 1;
		state.stack[0] = path_frame{tree, current};
		return lookup_from(tree, current, path, state);
	}

	[[nodiscard]] static frozen_text_tree_ptr create_from(
		std::vector<detail::node_record>& nodes,
		std::vector<detail::edge_record>& edges,
		std::vector<frozen_text_tree_ptr>& tree_ptrs,
		std::string& strings,
		node_id root) {
		const auto layout = make_payload_layout(nodes.size(), edges.size(), tree_ptrs.size(), strings.size());
		const auto allocation_size = payload_offset() + layout.total_size;
		auto* memory = static_cast<std::byte*>(::operator new(allocation_size, std::align_val_t{allocation_alignment()}));

		frozen_text_tree* tree{};
		frozen_text_tree_ptr* ptr_dst{};
		std::size_t constructed_tree_ptrs{};
		try {
			tree = new (static_cast<void*>(memory)) frozen_text_tree{layout.total_size, root};
			auto* raw = tree->payload_data();

			auto* node_dst = reinterpret_cast<detail::node_record*>(raw + layout.nodes_offset);
			for(std::size_t i = 0; i < nodes.size(); ++i) {
				std::construct_at(node_dst + i, nodes[i]);
			}

			auto* edge_dst = reinterpret_cast<detail::edge_record*>(raw + layout.edges_offset);
			for(std::size_t i = 0; i < edges.size(); ++i) {
				std::construct_at(edge_dst + i, edges[i]);
			}

			ptr_dst = reinterpret_cast<frozen_text_tree_ptr*>(raw + layout.ptrs_offset);
			for(std::size_t i = 0; i < tree_ptrs.size(); ++i) {
				std::construct_at(ptr_dst + i, std::move(tree_ptrs[i]));
				++constructed_tree_ptrs;
			}

			auto* str_dst = reinterpret_cast<char*>(raw + layout.strings_offset);
			std::ranges::copy(strings, str_dst);

			tree->nodes_ = std::span<const detail::node_record>{node_dst, nodes.size()};
			tree->edges_ = std::span<const detail::edge_record>{edge_dst, edges.size()};
			tree->tree_ptrs_ = std::span<const frozen_text_tree_ptr>{ptr_dst, tree_ptrs.size()};
			tree->strings_ = str_dst;
			tree->strings_size_ = strings.size();

			return frozen_text_tree_ptr{tree};
		} catch(...) {
			for(std::size_t i = 0; i < constructed_tree_ptrs; ++i) {
				std::destroy_at(ptr_dst + i);
			}
			if(tree != nullptr) {
				tree->~frozen_text_tree();
			}
			::operator delete(memory, std::align_val_t{allocation_alignment()});
			throw;
		}
	}

	static void destroy_heap(frozen_text_tree* tree) noexcept {
		if(tree == nullptr) {
			return;
		}
		tree->~frozen_text_tree();
		::operator delete(static_cast<void*>(tree), std::align_val_t{allocation_alignment()});
	}
};

inline void frozen_text_tree_deleter::operator()(frozen_text_tree* tree) const noexcept {
	frozen_text_tree::destroy_heap(tree);
}

class text_tree_cursor {
public:
	[[nodiscard]] text_tree_cursor() = default;

	[[nodiscard]] explicit operator bool() const noexcept {
		return status_ == lookup_status::ok;
	}

	[[nodiscard]] lookup_status status() const noexcept {
		return status_;
	}

	[[nodiscard]] node_kind kind() const noexcept {
		return result().kind;
	}

	[[nodiscard]] node_id node() const noexcept {
		return node_;
	}

	[[nodiscard]] const frozen_text_tree* tree() const noexcept {
		return tree_;
	}

	[[nodiscard]] lookup_result result() const noexcept {
		if(status_ != lookup_status::ok || tree_ == nullptr || node_ == invalid_node_id) {
			return {.status = status_};
		}
		return tree_->result_for(tree_, node_);
	}

	[[nodiscard]] std::optional<std::string_view> text() const noexcept {
		const auto value = result();
		if(value.is_text()) {
			return value.text;
		}
		return std::nullopt;
	}

	[[nodiscard]] text_tree_cursor operator[](std::string_view path) const noexcept {
		if(status_ != lookup_status::ok || tree_ == nullptr) {
			return text_tree_cursor{tree_, node_, status_};
		}
		return text_tree_cursor{tree_->lookup_from_node(tree_, node_, path)};
	}

	[[nodiscard]] operator lookup_result() const noexcept {
		return result();
	}

private:
	friend class frozen_text_tree;

	const frozen_text_tree* tree_{};
	node_id node_{invalid_node_id};
	lookup_status status_{lookup_status::missing};

	[[nodiscard]] explicit text_tree_cursor(const lookup_result& result) noexcept
		: tree_(result.tree),
		  node_(result.node),
		  status_(result.status) {
	}

	[[nodiscard]] text_tree_cursor(const frozen_text_tree* tree, node_id node, lookup_status status) noexcept
		: tree_(tree),
		  node_(node),
		  status_(status) {
	}
};

inline text_tree_cursor frozen_text_tree::operator[](std::string_view path) const noexcept {
	return text_tree_cursor{lookup(path)};
}

class text_tree_builder {
public:
	text_tree_builder() {
		nodes_.push_back(detail::build_node{.kind = node_kind::directory});
	}

	[[nodiscard]] namespace_id root_namespace() const noexcept {
		return 0;
	}

	[[nodiscard]] namespace_id create_namespace() {
		return new_node(node_kind::directory, invalid_node_id, {});
	}

	void make_dir(std::string_view path) {
		(void)ensure_namespace_path(root_namespace(), path_scratch_, path);
	}

	void make_dir(namespace_id root, std::string_view path) {
		(void)ensure_namespace_path(root, path_scratch_, path);
	}

	void set_text(std::string_view path, std::string_view text) {
		set_text(root_namespace(), path, text);
	}

	void set_text(namespace_id root, std::string_view path, std::string_view text) {
		const auto parsed = parse_path(path_scratch_, path);
		const auto parent = ensure_parent_path(root, parsed);
		const auto leaf = parsed.back();
		if(auto itr = nodes_[parent].children.find(leaf); itr != nodes_[parent].children.end()) {
			if(itr->second.kind != detail::edge_kind::direct) {
				throw std::invalid_argument{"cannot replace symbolic link with text"};
			}
			auto& target = nodes_.at(itr->second.target);
			if(target.kind != node_kind::text) {
				throw std::invalid_argument{"text path conflicts with existing namespace node"};
			}
			target.text.assign(text);
			return;
		}

		const auto id = new_node(node_kind::text, parent, leaf);
		nodes_[id].text.assign(text);
		nodes_[parent].children.emplace(std::string{leaf}, detail::build_edge{.target = id});
	}

	void mount_namespace(std::string_view path, namespace_id mounted_root) {
		mount_namespace(root_namespace(), path, mounted_root);
	}

	void mount_namespace(namespace_id root, std::string_view path, namespace_id mounted_root) {
		check_namespace(mounted_root);
		const auto parsed = parse_path(path_scratch_, path);
		const auto parent = ensure_parent_path(root, parsed);
		if(reachable(mounted_root, parent)) {
			throw std::invalid_argument{"namespace mount would create a structural cycle"};
		}
		insert_direct_edge(parent, parsed.back(), mounted_root, "namespace mount path already exists");
	}

	void mount_tree(std::string_view path, frozen_text_tree_ptr tree) {
		mount_tree(root_namespace(), path, std::move(tree));
	}

	void mount_tree(namespace_id root, std::string_view path, frozen_text_tree_ptr tree) {
		if(!tree) {
			throw std::invalid_argument{"mounted frozen_text_tree_ptr cannot be null"};
		}
		const auto parsed = parse_path(path_scratch_, path);
		const auto parent = ensure_parent_path(root, parsed);
		const auto leaf = parsed.back();
		const auto id = new_node(node_kind::tree_pointer, parent, leaf);
		nodes_[id].tree = std::move(tree);
		insert_direct_edge(parent, leaf, id, "tree pointer mount path already exists");
	}

	void add_hard_link(std::string_view path, std::string_view target_path) {
		add_hard_link(root_namespace(), path, root_namespace(), target_path);
	}

	void add_hard_link(namespace_id root, std::string_view path, namespace_id target_root, std::string_view target_path) {
		const auto target = resolve_direct_node(target_root, target_path);
		const auto parsed = parse_path(path_scratch_, path);
		const auto parent = ensure_parent_path(root, parsed);
		if(detail::is_local_namespace(nodes_[target].kind) && reachable(target, parent)) {
			throw std::invalid_argument{"hard link would create a structural cycle"};
		}
		insert_direct_edge(parent, parsed.back(), target, "hard link path already exists");
	}

	void add_symbolic_link(std::string_view path, std::string_view target) {
		add_symbolic_link(root_namespace(), path, target);
	}

	void add_symbolic_link(namespace_id root, std::string_view path, std::string_view target) {
		validate_link_target(target);
		const auto parsed = parse_path(path_scratch_, path);
		const auto parent = ensure_parent_path(root, parsed);
		const auto leaf = parsed.back();
		if(nodes_[parent].children.contains(leaf)) {
			throw std::invalid_argument{"symbolic link path already exists"};
		}
		nodes_[parent].children.emplace(
			std::string{leaf},
			detail::build_edge{.kind = detail::edge_kind::symbolic_link, .link_target = std::string{target}});
	}

	[[nodiscard]] frozen_text_tree_ptr freeze() && {
		std::vector<detail::node_record> frozen_nodes;
		std::vector<detail::edge_record> frozen_edges;
		std::vector<frozen_text_tree_ptr> frozen_tree_ptrs;
		std::string frozen_strings;

		frozen_nodes.resize(nodes_.size());
		for(node_id id = 0; id < nodes_.size(); ++id) {
			auto& src = nodes_[id];
			auto& dst = frozen_nodes[id];
			dst.kind = src.kind;
			if(src.kind == node_kind::text) {
				dst.text_offset = append_string(frozen_strings, src.text);
				dst.text_size = checked_u32(src.text.size(), "text too large");
			} else if(src.kind == node_kind::tree_pointer) {
				dst.tree_ptr_index = checked_u32(frozen_tree_ptrs.size(), "too many mounted text trees");
				frozen_tree_ptrs.push_back(std::move(src.tree));
			}
		}

		for(node_id id = 0; id < nodes_.size(); ++id) {
			const auto& src = nodes_[id];
			auto& dst = frozen_nodes[id];
			dst.edge_begin = checked_u32(frozen_edges.size(), "too many text tree edges");
			dst.edge_count = checked_u32(src.children.size(), "too many child edges");

			for(const auto& [name, edge] : src.children) {
				detail::edge_record out{};
				out.hash = detail::ascii_hash64(name);
				out.name_offset = append_string(frozen_strings, name);
				out.name_size = checked_u32(name.size(), "edge name too large");
				out.kind = edge.kind;
				out.target = edge.target;
				if(edge.kind == detail::edge_kind::symbolic_link) {
					out.link_offset = append_string(frozen_strings, edge.link_target);
					out.link_size = checked_u32(edge.link_target.size(), "symbolic link target too large");
				}
				frozen_edges.push_back(out);
			}

			auto range = std::ranges::subrange{
				frozen_edges.begin() + dst.edge_begin,
				frozen_edges.begin() + dst.edge_begin + dst.edge_count};
			std::ranges::sort(range, [&frozen_strings](const detail::edge_record& lhs, const detail::edge_record& rhs) {
				if(lhs.hash != rhs.hash) {
					return lhs.hash < rhs.hash;
				}
				const auto lhs_name = std::string_view{frozen_strings.data() + lhs.name_offset, lhs.name_size};
				const auto rhs_name = std::string_view{frozen_strings.data() + rhs.name_offset, rhs.name_size};
				return lhs_name < rhs_name;
			});
		}

		auto frozen = frozen_text_tree::create_from(
			frozen_nodes,
			frozen_edges,
			frozen_tree_ptrs,
			frozen_strings,
			root_namespace());
		validate_symbolic_links(*frozen);
		return frozen;
	}

private:
	std::vector<detail::build_node> nodes_{};
	mutable std::vector<std::string_view> path_scratch_{};
	mutable std::vector<std::string_view> target_path_scratch_{};
	mutable std::vector<node_id> reach_stack_{};
	mutable std::vector<std::uint32_t> reach_marks_{};
	mutable std::uint32_t reach_epoch_{1};

	[[nodiscard]] static std::uint32_t checked_u32(std::size_t value, const char* message) {
		if(value > std::numeric_limits<std::uint32_t>::max()) {
			throw std::length_error{message};
		}
		return static_cast<std::uint32_t>(value);
	}

	[[nodiscard]] static std::uint32_t append_string(std::string& buffer, std::string_view value) {
		const auto offset = checked_u32(buffer.size(), "text tree string pool too large");
		buffer.append(value);
		buffer.push_back('\0');
		return offset;
	}

	[[nodiscard]] node_id new_node(node_kind kind, node_id parent, std::string_view name) {
		const auto id = checked_u32(nodes_.size(), "too many text tree nodes");
		nodes_.push_back(detail::build_node{
			.kind = kind,
			.canonical_parent = parent,
			.canonical_name = std::string{name},
		});
		return id;
	}

	void check_node(node_id id) const {
		if(id >= nodes_.size()) {
			throw std::out_of_range{"text tree node id out of range"};
		}
	}

	void check_namespace(namespace_id id) const {
		check_node(id);
		if(!detail::is_local_namespace(nodes_[id].kind)) {
			throw std::invalid_argument{"node is not a text namespace"};
		}
	}

	[[nodiscard]] std::span<const std::string_view> parse_path(
		std::vector<std::string_view>& scratch,
		std::string_view path) const {
		scratch.clear();
		if(!detail::valid_dotted_path(path)) {
			throw std::invalid_argument{"invalid text tree path"};
		}
		while(!path.empty()) {
			const auto segment = detail::next_segment(path);
			if(!segment.valid) {
				throw std::invalid_argument{"invalid text tree path"};
			}
			scratch.push_back(segment.value);
			path = segment.remain;
		}
		return scratch;
	}

	[[nodiscard]] node_id ensure_parent_path(namespace_id root, std::span<const std::string_view> parsed) {
		if(parsed.empty()) {
			throw std::invalid_argument{"text tree path cannot be empty"};
		}
		check_namespace(root);
		node_id current = root;
		for(const auto segment : parsed.first(parsed.size() - 1)) {
			current = ensure_child_namespace(current, segment);
		}
		return current;
	}

	[[nodiscard]] node_id ensure_namespace_path(
		namespace_id root,
		std::vector<std::string_view>& scratch,
		std::string_view path) {
		const auto parsed = parse_path(scratch, path);
		check_namespace(root);
		node_id current = root;
		for(const auto segment : parsed) {
			current = ensure_child_namespace(current, segment);
		}
		return current;
	}

	[[nodiscard]] node_id ensure_child_namespace(node_id current, std::string_view segment) {
		if(!detail::is_local_namespace(nodes_.at(current).kind)) {
			throw std::invalid_argument{"cannot create child under non-local namespace node"};
		}

		if(auto itr = nodes_[current].children.find(segment); itr != nodes_[current].children.end()) {
			if(itr->second.kind != detail::edge_kind::direct) {
				throw std::invalid_argument{"cannot create child under symbolic link"};
			}
			const auto child = itr->second.target;
			if(!detail::is_local_namespace(nodes_.at(child).kind)) {
				throw std::invalid_argument{"path segment conflicts with non-namespace node"};
			}
			return child;
		}

		const auto child = new_node(node_kind::directory, current, segment);
		nodes_[current].children.emplace(std::string{segment}, detail::build_edge{.target = child});
		return child;
	}

	void insert_direct_edge(node_id parent, std::string_view name, node_id target, const char* conflict_message) {
		check_node(target);
		if(!detail::is_local_namespace(nodes_.at(parent).kind)) {
			throw std::invalid_argument{"cannot insert child under non-local namespace node"};
		}
		if(nodes_[parent].children.contains(name)) {
			throw std::invalid_argument{conflict_message};
		}
		nodes_[parent].children.emplace(std::string{name}, detail::build_edge{.target = target});
	}

	[[nodiscard]] node_id resolve_direct_node(namespace_id root, std::string_view path) const {
		check_namespace(root);
		const auto parsed = parse_path(target_path_scratch_, path);
		node_id current = root;
		for(const auto segment : parsed) {
			const auto& node = nodes_.at(current);
			if(!detail::is_local_namespace(node.kind)) {
				throw std::invalid_argument{"target path enters a non-local namespace node"};
			}
			const auto itr = node.children.find(segment);
			if(itr == node.children.end()) {
				throw std::invalid_argument{"hard link target does not exist"};
			}
			if(itr->second.kind != detail::edge_kind::direct) {
				throw std::invalid_argument{"hard link target cannot be a symbolic link"};
			}
			current = itr->second.target;
		}
		return current;
	}

	[[nodiscard]] bool reachable(node_id from, node_id target) const {
		if(reach_marks_.size() < nodes_.size()) {
			reach_marks_.resize(nodes_.size());
		}
		if(++reach_epoch_ == 0) {
			std::ranges::fill(reach_marks_, 0U);
			reach_epoch_ = 1;
		}

		reach_stack_.clear();
		reach_stack_.push_back(from);
		while(!reach_stack_.empty()) {
			const auto current = reach_stack_.back();
			reach_stack_.pop_back();
			if(current == target) {
				return true;
			}
			if(reach_marks_[current] == reach_epoch_) {
				continue;
			}
			reach_marks_[current] = reach_epoch_;
			const auto& node = nodes_[current];
			if(!detail::is_local_namespace(node.kind)) {
				continue;
			}
			for(const auto& [name, edge] : node.children) {
				(void)name;
				if(edge.kind == detail::edge_kind::direct) {
					reach_stack_.push_back(edge.target);
				}
			}
		}
		return false;
	}

	static void validate_link_target(std::string_view target) {
		if(target.empty()) {
			throw std::invalid_argument{"symbolic link target cannot be empty"};
		}

		if(target.front() == '/') {
			target.remove_prefix(1);
		} else {
			while(true) {
				if(target == "." || target == "..") {
					return;
				}
				if(target.starts_with("./")) {
					target.remove_prefix(2);
					continue;
				}
				if(target.starts_with("../")) {
					target.remove_prefix(3);
					if(target.empty()) {
						return;
					}
					continue;
				}
				break;
			}
		}

		if(target.empty() || !detail::valid_dotted_path(target)) {
			throw std::invalid_argument{"invalid symbolic link target"};
		}
	}

	[[nodiscard]] std::vector<frozen_text_tree::path_frame> canonical_stack(
		const frozen_text_tree& frozen,
		node_id id) const {
		std::vector<frozen_text_tree::path_frame> reversed;
		for(node_id current = id; current != invalid_node_id; current = nodes_[current].canonical_parent) {
			reversed.push_back({std::addressof(frozen), current});
		}
		std::ranges::reverse(reversed);
		if(reversed.empty() || reversed.front().node != root_namespace()) {
			reversed.insert(reversed.begin(), {std::addressof(frozen), root_namespace()});
		}
		if(reversed.size() > frozen_text_tree::max_path_depth) {
			throw std::invalid_argument{"canonical path too deep for symbolic link validation"};
		}
		return reversed;
	}

	void validate_symbolic_links(const frozen_text_tree& frozen) const {
		for(node_id owner = 0; owner < nodes_.size(); ++owner) {
			const auto& node = nodes_[owner];
			for(const auto& [name, edge] : node.children) {
				(void)name;
				if(edge.kind != detail::edge_kind::symbolic_link) {
					continue;
				}

				frozen_text_tree::resolver_state state{};
				const auto stack = canonical_stack(frozen, owner);
				state.depth = stack.size();
				std::ranges::copy(stack, state.stack.begin());
				const auto result = frozen.resolve_link_target(std::addressof(frozen), owner, edge.link_target, state);
				if(!result) {
					throw std::invalid_argument{
						std::format("invalid symbolic link target: {}", detail::status_name(result.status))};
				}
			}
		}
	}
};

}
