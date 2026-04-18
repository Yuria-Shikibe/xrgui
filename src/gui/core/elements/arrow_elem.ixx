//
// Created by Matrix on 2026/4/18.
//

export module mo_yanxi.gui.elem.arrow_elem;

import std;
export import mo_yanxi.gui.infrastructure;

import mo_yanxi.gui.util.animator;
import mo_yanxi.gui.fx.compound;
import mo_yanxi.gui.fx.fringe;
import mo_yanxi.math;
import mo_yanxi.math.interpolation;

namespace mo_yanxi::gui{
export
struct arrow_elem_config{
	float margin = 4.f;
	float length = 16.f;
	float stroke = 1.75f;

};

using arrow_animator = util::animator<float, 1 / 20.f, 0, 0>;
export
struct arrow_button : elem {
	protected:
    arrow_animator rotate_animator{};

	public:
	math::range rotate_angle{0, math::pi_half};
	arrow_elem_config config{};

    bool update(float delta_in_ticks) override {
       if(elem::update(delta_in_ticks)){
          if(rotate_animator.is_animating()) {
             rotate_animator.update(delta_in_ticks,
                [this] { util::update_erase(*this, update_channel::draw); }, // expanded
                [this] { util::update_erase(*this, update_channel::draw); }  // closed
             );
          } else {
             util::update_erase(*this, update_channel::draw);
          }
          return true;
       }
       return false;
    }

public:
	using elem::elem;


    template <typename S, std::invocable<S&> EnterFn, std::invocable<S&> ExitFn>
    void arrow_flip(this S& self, EnterFn&& onEnter, ExitFn&& onExit){
    	arrow_animator& a = self.rotate_animator;
    	if(!a.is_animating())util::update_insert(self, update_channel::draw);
	    if(a.is_tobe_idle()){
    		if(!a.set_target(true))return;
	    	std::invoke(std::forward<EnterFn>(onEnter), self);
	    } else{
	    	if(!a.set_target(false))return;
	    	std::invoke(std::forward<ExitFn>(onExit), self);
	    }
    }

    template <typename S, std::invocable<S&> EnterFn>
    void arrow_enter(this S& self, EnterFn&& onEnter){
    	arrow_animator& a = self.rotate_animator;

    	if(!a.is_tobe_active())util::update_insert(self, update_channel::draw);
    	if(!a.set_target(true))return;
    	std::invoke(std::forward<EnterFn>(onEnter), self);
    }

    template <typename S, std::invocable<S&> ExitFn>
    void arrow_exit(this S& self, ExitFn&& onExit){
    	arrow_animator& a = self.rotate_animator;

    	if(!a.is_tobe_idle())util::update_insert(self, update_channel::draw);
    	if(!a.set_target(false))return;
    	std::invoke(std::forward<ExitFn>(onExit), self);
    }

    void record_draw_layer(draw_call_stack_recorder& call_stack_builder) const override {
       elem::record_draw_layer(call_stack_builder);
       call_stack_builder.push_call_noop(*this, [](const arrow_button& s, const draw_call_param& p) static {
          if(!p.layer_param.is_top()) return;
          if(!util::is_draw_param_valid(s, p)) return;

          auto arrow = fx::compound::generate_centered_arrow(s.content_extent().fdim({s.config.margin, s.config.margin}), s.config.stroke, s.config.length);
          fx::fringe::inplace_line_context<(7 + 4) * 2> context{};

          // 从 anim_ 获取进度
          float prog = s.rotate_animator.get_progress() | math::interp::smoother;
          auto [cos, sin] = math::cos_sin(s.rotate_angle[prog]);
          for(auto vertex : arrow.vertices){
             context.push(vertex.rotate(cos, sin) + s.content_bound_abs().get_center(), arrow.thick,
                       graphic::colors::white.copy_set_a(s.get_draw_opacity()));
          }

          context.add_cap();
          context.add_fringe_cap();
          context.dump_mid(s.renderer(), graphic::draw::instruction::line_segments{});
          context.dump_fringe_inner(s.renderer(), graphic::draw::instruction::line_segments{});
          context.dump_fringe_outer(s.renderer(), graphic::draw::instruction::line_segments{});
       });
    }

};
}
