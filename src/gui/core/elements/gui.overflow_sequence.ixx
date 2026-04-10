export module mo_yanxi.gui.elem.overflow_sequence;

import mo_yanxi.gui.elem.sequence;
import std;

namespace mo_yanxi::gui {

export
template <typename Elem, typename Cell>
struct overflow_sequence_create_result{
	Elem& elem;
	Cell& cell;
};

export
struct overflow_sequence : sequence {
private:
	std::size_t split_index_{0};
	
	// 溢出时显示的替代元素 (E) 及其对应的 cell 适配器
	elem_ptr overflow_elem_{};
	cell_adaptor<layout::partial_mastering_cell> overflow_cell_{};

	// 实际暴露给外界和绘制管线的入选子元素与 Cell
	mr::heap_vector<elem*> exposed_children_{get_heap_allocator<elem*>()};
	mr::heap_vector<adaptor_type*> exposed_cells_{get_heap_allocator<adaptor_type*>()};
	mr::heap_vector<elem*> old_exposed_children_{get_heap_allocator<elem*>()};

	bool is_overflowed_{false};
	bool requires_scissor_{false};

public:
	[[nodiscard]] overflow_sequence(scene& scene, elem* parent)
		: sequence(scene, parent){
		set_expand_policy(layout::expand_policy::passive);
	}
protected:
	// [新增] 辅助函数：安全地将即将被移除的元素从曝光列表中剥离，并发送 false 通知
	void notify_and_remove_exposed(elem* target) {
		if (!target) return;

		auto it = std::ranges::find(exposed_children_, target);
		if (it != exposed_children_.end()) {
			// 1. 发送移出通知
			target->on_display_state_changed(false, false);

			// 2. 同步清理 exposed_children_ 和 exposed_cells_，避免下次 layout_elem 前出现悬垂访问
			std::size_t idx = std::distance(exposed_children_.begin(), it);
			exposed_children_.erase(it);

			if (idx < exposed_cells_.size()) {
				exposed_cells_.erase(exposed_cells_.begin() + idx);
			}
		}
	}

public:
	void erase_afterward(std::size_t where) override {
		if (where < cells_.size()) {
			notify_and_remove_exposed(cells_[where].element);
		}
		sequence::erase_afterward(where);
	}

	void erase_instantly(std::size_t where) override {
		if (where < cells_.size()) {
			notify_and_remove_exposed(cells_[where].element);
		}
		sequence::erase_instantly(where);
	}

	elem_ptr exchange(std::size_t where, elem_ptr&& elem, bool force_isolated_notify) override {
		if (where < cells_.size()) {
			notify_and_remove_exposed(cells_[where].element);
		}
		return sequence::exchange(where, std::move(elem), force_isolated_notify);
	}

	// 1. 设置 Prev 和 Post 的划分下标
	void set_split_index(std::size_t index) {
		if (util::try_modify(split_index_, index)) {
			notify_isolated_layout_changed();
		}
	}

	void set_overflow_elem(elem_ptr&& elem) {
		// [新增] 安全防护：如果旧的 overflow_elem_ 正在展示，替换前需发送移出通知并安全抹除，防止悬垂
		if (overflow_elem_) {
			for (auto& p : exposed_children_) {
				if (p == overflow_elem_.get()) {
					p->on_display_state_changed(false, false);
					p = nullptr; // 置空，防止在下次 layout_elem 时重复发送或访问非法内存
				}
			}
		}

		overflow_elem_ = std::move(elem);
		if (overflow_elem_) {
			overflow_elem_->set_parent(this);
			overflow_elem_->update_abs_src(content_src_pos_abs());
			overflow_cell_.element = overflow_elem_.get();
			overflow_cell_.cell = template_cell;
		}
		notify_isolated_layout_changed();
	}

	template <invocable_elem_init_func Fn, typename... Args>
	overflow_sequence_create_result<elem_init_func_create_t<Fn>, layout::partial_mastering_cell> create_overflow_elem(Fn&& init, Args&&... args){
		elem_ptr eptr{get_scene(), this, std::forward<Fn>(init), std::forward<Args>(args)...};
		set_overflow_elem(std::move(eptr));
		return overflow_sequence_create_result{static_cast<elem_init_func_create_t<Fn>&>(*overflow_elem_), overflow_cell_.cell};
	}

	template <std::invocable<layout::partial_mastering_cell&> Fn>
	void modify_overflow_elem_cell(Fn&& fn){
		if constexpr (std::predicate<Fn&&, layout::partial_mastering_cell&>){
			if(std::invoke(std::forward<Fn>(fn), overflow_cell_.cell)){
				notify_isolated_layout_changed();
			}
		}else{
			std::invoke(std::forward<Fn>(fn), overflow_cell_.cell);
			notify_isolated_layout_changed();
		}
	}

	// 4. 获取是否需要插入 scissor
	[[nodiscard]] bool is_scissor_required() const noexcept {
		return requires_scissor_;
	}

	[[nodiscard]] bool is_overflowed() const noexcept {
		return is_overflowed_;
	}

	// 核心：重写 children() 以便 draw_children 只渲染活跃的元素
	[[nodiscard]] elem_span children() const noexcept final {
		// elem_span 支持从连续的 elem* 范围构造
		return elem_span{exposed_children_.data(), exposed_children_.size()};
	}

	bool update_abs_src(math::vec2 parent_content_src) noexcept override {
		if (sequence::update_abs_src(parent_content_src)) {
			if (overflow_elem_) {
				overflow_elem_->update_abs_src(content_src_pos_abs());
			}
			return true;
		}
		return false;
	}

	void layout_elem() override {
		// [修改] 复用类成员的缓存空间
		old_exposed_children_.clear();
		for (auto* e : exposed_children_) {
			// 过滤掉可能因为被替换而被置空的指针
			if (e) old_exposed_children_.push_back(e);
		}

		exposed_children_.clear();
		exposed_cells_.clear();

		if (cells_.empty()) {
			is_overflowed_ = false;
			requires_scissor_ = false;
			if (auto pref = get_prefer_extent(); pref && get_expand_policy() == layout::expand_policy::prefer) {
				resize(pref.value(), propagate_mask::force_upper);
			}

			// 元素全空，旧的曝光元素全部移出
			for (auto* e : old_exposed_children_) {
				e->on_display_state_changed(false, false);
			}
			// [重要] 别忘了清空缓存，防止持有悬垂指针
			old_exposed_children_.clear();
			return;
		}

		auto [majorTarget, minorTarget] = layout::get_vec_ptr(get_layout_policy());
		auto content_sz = content_extent();
		float major_size = content_sz.*majorTarget;
		float minor_scaling = get_scaling().*minorTarget;

		float max_minor = restriction_extent.potential_extent().*minorTarget;
		if (get_expand_policy() == layout::expand_policy::passive) {
			max_minor = std::min(max_minor, content_sz.*minorTarget);
		}

		mr::heap_vector<float> exposed_sizes{get_heap_allocator<float>()};

		auto info = calculate_overflow_layout(
			major_size, minor_scaling, max_minor,
			&exposed_children_, &exposed_cells_, &exposed_sizes
		);

		// 1. 寻找被移出的元素 (在 old_exposed_children_ 中，但不在 exposed_children_ 中)
		for (auto* e : old_exposed_children_) {
			if (std::ranges::find(exposed_children_, e) == exposed_children_.end()) {
				e->on_display_state_changed(false, false);
			}
		}
		// 2. 寻找被移入的元素 (在 exposed_children_ 中，但不在 old_exposed_children_ 中)
		for (auto* e : exposed_children_) {
			if (std::ranges::find(old_exposed_children_, e) == old_exposed_children_.end()) {
				e->on_display_state_changed(true, false);
			}
		}

		old_exposed_children_.clear();

		is_overflowed_ = info.is_overflowed;
		requires_scissor_ = info.requires_scissor;

		// 重新调整自身尺寸
		if (get_expand_policy() != layout::expand_policy::passive) {
			math::vec2 size;
			size.*majorTarget = content_sz.*majorTarget;
			size.*minorTarget = info.masterings;
			size += boarder().extent();

			if (get_expand_policy() == layout::expand_policy::prefer) {
				size.max(get_prefer_extent().value_or(math::vec2{}));
			}

			size.min(restriction_extent.potential_extent());
			resize(size, propagate_mask::force_upper);
			content_sz = content_extent();
		}

		// 布局放置被选中的子元素
		float minor_offset = (is_align_to_tail() && info.passives == 0) ? content_sz.*minorTarget - info.masterings : 0;
		const auto remains = std::fdim(content_sz.*minorTarget, info.masterings);
		const auto passive_unit = info.passives > 0 ? remains / info.passives : 0;

		math::vec2 currentOff{};
		if (!exposed_cells_.empty()) {
			currentOff.*minorTarget = minor_offset - exposed_cells_.front()->cell.pad.pre;

			for (std::size_t i = 0; i < exposed_cells_.size(); ++i) {
				auto* cell = exposed_cells_[i];
				currentOff.*minorTarget += cell->cell.pad.pre;

				auto minor = exposed_sizes[i];
				if (cell->cell.stated_size.type == layout::size_category::passive) {
					minor *= passive_unit;
				}

				math::vec2 cell_sz;
				cell_sz.*majorTarget = content_sz.*majorTarget;
				cell_sz.*minorTarget = minor;

				cell->cell.allocated_region = {tags::from_extent, currentOff, cell_sz};

				cell_sz.*majorTarget = this->restriction_extent.potential_extent().*majorTarget;
				if (cell->cell.stated_size.pending()) cell_sz.*minorTarget = std::numeric_limits<float>::infinity();

				cell->apply(*this, cell_sz);

				auto pref = cell_sz;
				pref.*minorTarget = 0;
				cell->element->set_prefer_extent(pref);
				if (!is_pos_smooth()) cell->cell.update_relative_src(*cell->element, content_src_pos_abs());

				currentOff.*minorTarget += cell->cell.pad.post + minor;
			}
		}
	}

	void clear() noexcept override {
		sequence::clear();
		exposed_cells_.clear();
		exposed_children_.clear();
		notify_isolated_layout_changed();
	}

	virtual void record_draw_layer(draw_call_stack_recorder& call_stack_builder){
		push_draw_func_to_stack_recorder(call_stack_builder);
	}

	void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override {
		elem::draw_layer(clipSpace, param);
		const auto space = content_bound_abs().intersection_with(clipSpace);

		if (requires_scissor_) {
			renderer().push_scissor({content_bound_abs()});
			renderer().notify_viewport_changed();
		}

		// 这里调用 base_group 的逻辑，它会自动调用被我们重写过的 children() 方法，因此只会绘制被暴露的元素
		draw_children(space, param);

		if (requires_scissor_) {
			renderer().pop_scissor();
			renderer().notify_viewport_changed();
		}
	}

protected:
	// 重写绘制流程，在超出时预留 Scissor

protected:
	struct overflow_layout_info {
		bool is_overflowed{false};
		bool requires_scissor{false};
		float masterings{0.f};
		float passives{0.f};
	};

	// 统一的布局测量与截断算法
	overflow_layout_info calculate_overflow_layout(
		float major_size,
		float minor_scaling,
		float max_minor,
		mr::heap_vector<elem*>* out_children = nullptr,
		mr::heap_vector<adaptor_type*>* out_cells = nullptr,
		mr::heap_vector<float>* out_inner_sizes = nullptr
	) {
		overflow_layout_info info{};
		auto [majorTarget, minorTarget] = layout::get_vec_ptr(get_layout_policy());

		// [修复 1] 严格限定只获取 Inner 内容大小，绝不混入 Padding
		auto calc_inner_minor = [&](elem* e, const adaptor_type& c) {
			float sz = 0.f;
			switch (c.cell.stated_size.type) {
				case layout::size_category::pending: {
					math::vec2 vec;
					vec.*majorTarget = major_size;
					vec.*minorTarget = std::numeric_limits<float>::infinity();
					sz = e->pre_acquire_size(vec).value_or(math::vec2{}).*minorTarget;
					break;
				}
				case layout::size_category::mastering:
					sz = c.cell.stated_size.value * minor_scaling;
					break;
				case layout::size_category::passive:
					break;
				case layout::size_category::scaling:
					sz = c.cell.stated_size.value * major_size;
					break;
			}
			return sz;
		};

		// 预计算所有潜在元素的 Inner Size
		mr::heap_vector<float> inner_sizes{get_heap_allocator<float>()};
		inner_sizes.reserve(cells_.size());
		float sum_all_minor = 0.f;

		for (auto& cell : cells_) {
			float inner = calc_inner_minor(cell.element, cell);
			inner_sizes.push_back(inner);
			// 累加总物理占空：inner + pad.pre + pad.post
			sum_all_minor += inner + cell.cell.pad.length();
		}

		float all_actual_minor = sum_all_minor;
		if (!cells_.empty()) {
			// 扣除两端边距以对齐基类的折叠逻辑
			all_actual_minor -= cells_.front().cell.pad.pre;
			all_actual_minor -= cells_.back().cell.pad.post;
		}

		const std::size_t valid_split = std::min(split_index_, cells_.size());

		float first_pad_pre = 0.f;
		float last_pad_post = 0.f;
		bool has_any = false;

		// 暴露元素的登记器
		auto add_exposed = [&](elem* e, adaptor_type* c, float inner_sz) {
			if (out_children) out_children->push_back(e);
			if (out_cells) out_cells->push_back(c);
			if (out_inner_sizes) out_inner_sizes->push_back(inner_sz); // 严格存入 Inner Size

			// 累加计算实际的 masterings (内部尺寸 + padding)
			info.masterings += inner_sz + c->cell.pad.length();
			if (c->cell.stated_size.type == layout::size_category::passive) {
				info.passives += c->cell.stated_size.value;
			}

			if (!has_any) {
				first_pad_pre = c->cell.pad.pre;
				has_any = true;
			}
			last_pad_post = c->cell.pad.post;
		};

		// 2. 截断判断与装载
		if (all_actual_minor <= max_minor || get_expand_policy() != layout::expand_policy::passive) {
			for (std::size_t i = 0; i < cells_.size(); ++i) {
				add_exposed(cells_[i].element, &cells_[i], inner_sizes[i]);
			}
		} else {
			info.is_overflowed = true;

			// --- 第一阶段：探测可容纳极限 ---
			float current_sum = 0.f;
			float current_first_pre = 0.f;

			// 预计算 Prev 尺寸
			for (std::size_t i = 0; i < valid_split; ++i) {
				current_sum += inner_sizes[i] + cells_[i].cell.pad.length();
				if (i == 0) current_first_pre = cells_[i].cell.pad.pre;
			}

			// 预计算 E 尺寸
			float e_inner = 0.f;
			if (overflow_elem_) {
				e_inner = calc_inner_minor(overflow_elem_.get(), overflow_cell_);
				current_sum += e_inner + overflow_cell_.cell.pad.length();
				if (valid_split == 0) current_first_pre = overflow_cell_.cell.pad.pre;
			}

			// 倒序探测 Post
			mr::heap_vector<std::size_t> post_indices{get_heap_allocator<std::size_t>()};
			for (std::size_t i = cells_.size(); i > valid_split; --i) {
				std::size_t idx = i - 1;
				float cell_total = inner_sizes[idx] + cells_[idx].cell.pad.length();

				// [修复 2] 动态预测加上该元素后的实际空间（扣除头尾的折叠边距）
				float candidate_sum = current_sum + cell_total;
				float candidate_actual = candidate_sum - current_first_pre - cells_[idx].cell.pad.post;

				if (candidate_actual <= max_minor) {
					current_sum = candidate_sum;
					post_indices.push_back(idx);
				} else {
					break;
				}
			}
			std::ranges::reverse(post_indices);

			// --- 第二阶段：真正执行装载 ---
			for (std::size_t i = 0; i < valid_split; ++i) {
				add_exposed(cells_[i].element, &cells_[i], inner_sizes[i]);
			}
			if (overflow_elem_) {
				add_exposed(overflow_elem_.get(), &overflow_cell_, e_inner);
			}
			for (std::size_t idx : post_indices) {
				add_exposed(cells_[idx].element, &cells_[idx], inner_sizes[idx]);
			}
		}

		// 3. 在暴露结束后，真正抵消实际首尾边距，得出需要告诉父级的精准空间
		if (has_any) {
			info.masterings -= first_pad_pre;
			info.masterings -= last_pad_post;
		}

		// 4. 精准检测是否需要 Scissor 裁剪
		if (info.is_overflowed && info.masterings > max_minor) {
			info.requires_scissor = true;
		}

		return info;
	}

protected:
	std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override {
		switch(get_layout_policy()){
		case layout::layout_policy::hori_major:
			if(extent.width_pending()) return std::nullopt;
			break;
		case layout::layout_policy::vert_major:
			if(extent.height_pending()) return std::nullopt;
			break;
		case layout::layout_policy::none:
			if(extent.fully_mastering()) return extent.potential_extent();
			return std::nullopt;
		default: std::unreachable();
		}

		if(cells_.empty()) return std::nullopt;

		auto potential = extent.potential_extent();
		const auto dep = extent.get_pending();
		auto [majorTargetDep, minorTargetDep] = layout::get_vec_ptr<bool>(get_layout_policy());

		if(dep.*minorTargetDep){
			auto [majorTarget, minorTarget] = layout::get_vec_ptr(get_layout_policy());

			if(get_expand_policy() == layout::expand_policy::passive){
				potential.*minorTarget = this->content_extent().*minorTarget;
			} else {
				float major_size = potential.*majorTarget;
				float minor_scaling = get_scaling().*minorTarget;
				// 在预申请阶段，max_minor 取 extent 的潜在大小（若是 pending 则为 inf）
				float max_minor = potential.*minorTarget;

				// 调用统一算法计算应该占据的副轴总长
				auto info = calculate_overflow_layout(major_size, minor_scaling, max_minor);
				potential.*minorTarget = info.masterings;
			}
		}

		if(auto pref = get_prefer_content_extent(); pref && get_expand_policy() == layout::expand_policy::prefer){
			potential.max(pref.value());
		}

		return potential;
	}
};

} // namespace mo_yanxi::gui