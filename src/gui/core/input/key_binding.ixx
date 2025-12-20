module;


#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include "plf_hive.h"
#endif

#include <mo_yanxi/adapted_attributes.hpp>
#include <mo_yanxi/enum_operator_gen.hpp>

export module mo_yanxi.input_handle:key_binding;

import :constants;
import mo_yanxi.referenced_ptr;
import mo_yanxi.refable_tuple;
import mo_yanxi.math.vector2;
import mo_yanxi.utility;

import mo_yanxi.meta_programming;
import std;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <plf_hive.h>;
#endif

namespace mo_yanxi::input_handle{

export
    template <typename... ParamTy>
    struct key_binding;

    export enum struct binding_return{
        none,
        // break_continuous = 1 << 0,
        reset_press_duration = 1 << 0,
    };

    // 原 key_binding<void> 现在作为独立的类型
    export
    struct any_key_binding : key_set{
        using function_ptr = void*;
        function_ptr func{};

        [[nodiscard]] any_key_binding(const key_set key, const function_ptr func)
            : key_set{key}, func(func){
        }

        [[nodiscard]] any_key_binding() = default;

        friend constexpr bool operator==(const any_key_binding& lhs, const any_key_binding& rhs) = default;

        template <typename... ParamTy>
        explicit operator key_binding<ParamTy...>() const noexcept;

        template <typename... ParamTy>
        FORCE_INLINE binding_return cast_and_exec(key_set actual, float press_dur, ParamTy... args) const;
    };

    BITMASK_OPS(export, binding_return);


    template <typename... ParamTy>
    struct key_binding : key_set{
        using params_type = std::tuple<ParamTy...>;
        using signature = binding_return(key_set, float, ParamTy...);
        using function_ptr = std::add_pointer_t<signature>;

        function_ptr func{};

        [[nodiscard]] key_binding(const key_set key, const function_ptr func)
            : key_set{key}, func(func){
        }

        [[nodiscard]] key_binding() = default;

        FORCE_INLINE binding_return operator()(float press_dur, ParamTy... args) const{
            CHECKED_ASSUME(func != nullptr);
            return func(*this, press_dur, std::forward<ParamTy>(args)...);
        }

        friend constexpr bool operator==(const key_binding& lhs, const key_binding& rhs) = default;

        // 与 any_key_binding 的比较操作
        friend constexpr bool operator==(const key_binding& lhs, const any_key_binding& rhs){
            return any_key_binding(lhs) == rhs;
        }

        friend constexpr bool operator==(const any_key_binding& lhs, const key_binding& rhs){
            return lhs == any_key_binding(rhs);
        }

        // 转换为擦除类型的操作
        explicit(false) operator any_key_binding() const noexcept{
            return {static_cast<key_set>(*this), reinterpret_cast<void*>(func)};
        }
    };

    // 延迟实现 any_key_binding 的模板方法，确保 key_binding 定义可见
    template <typename ... ParamTy>
    any_key_binding::operator key_binding<ParamTy...>() const noexcept{
        return std::bit_cast<key_binding<ParamTy...>>(*this);
    }

    template <typename ... ParamTy>
    binding_return any_key_binding::cast_and_exec(key_set actual, float press_dur, ParamTy... args) const{
        CHECKED_ASSUME(func != nullptr);
        CHECKED_ASSUME(actual.key_code == this->key_code);
        // 这里转换回具体的函数指针类型进行调用
        return static_cast<typename key_binding<ParamTy...>::function_ptr>(func)(actual, press_dur, std::forward<ParamTy>(args)...);
    }

    export
    enum struct pos_binding_target{
        scroll,
        cursor_absolute,
        cursor_delta,
        cursor_velocity,
        COUNT,
    };

    struct pos_binding{
        void* func{};

        [[nodiscard]] pos_binding() = default;
        [[nodiscard]] explicit pos_binding(void* func)
            : func(func){
        }

        template <typename ... ParamTy>
        using function_ptr = void(*)(math::vec2, ParamTy...);

        template <typename ... ParamTy>
        FORCE_INLINE void cast_and_exec(math::vec2 pos, ParamTy... args) const{
            CHECKED_ASSUME(func != nullptr);
            return static_cast<function_ptr<ParamTy...>>(func)(pos, std::forward<ParamTy>(args)...);
        }

        bool operator==(const pos_binding&) const noexcept = default;
        bool friend operator==(const pos_binding& s, const void* func) noexcept{
            return s.func == func;
        }

        bool friend operator==(const void* func, const pos_binding& s) noexcept{
            return s.func == func;
        }
    };

    struct inbound_binding{
        void* func{};

        [[nodiscard]] inbound_binding() = default;
        [[nodiscard]] explicit inbound_binding(void* func)
            : func(func){
        }

        template <typename ... ParamTy>
        using function_ptr = void(*)(bool, math::vec2, ParamTy...);

        template <typename ... ParamTy>
        FORCE_INLINE void cast_and_exec(bool inbounded, math::vec2 pos, ParamTy... args) const{
            CHECKED_ASSUME(func != nullptr);
            return static_cast<function_ptr<ParamTy...>>(func)(inbounded, pos, std::forward<ParamTy>(args)...);
        }

        bool operator==(const inbound_binding&) const noexcept = default;
        bool friend operator==(const inbound_binding& s, const void* func) noexcept{
            return s.func == func;
        }

        bool friend operator==(const void* func, const inbound_binding& s) noexcept{
            return s.func == func;
        }
    };

    enum struct act_state : std::uint8_t{
        released,
        pressed,
        repeating
    };

    export
    struct key_mapping_interface : protected referenced_object{
    public:
        // 类型别名修改为新的独立类型
        using bind_type = any_key_binding;

        struct binding_state{
            // gch::small_vector<bind_type, 3, std::pmr::polymorphic_allocator<bind_type>> bindings{};
            std::pmr::vector<bind_type> bindings{};
            float double_click_timer{};
            float press_duration{};
            act_state action_state{};


            template <typename ...ParamTy>
            FORCE_INLINE void trigger(refable_tuple<ParamTy ...>& context, act action, mode mode_bits){
                for (const auto & binding : bindings){
                    if(matched(action, binding.action) && matched(mode_bits, binding.mode_bits)){
                        const binding_return rst = [&, this]<std::size_t ...Idx>(std::index_sequence<Idx...>){
                            // any_key_binding 的 cast_and_exec 调用
                            return binding.cast_and_exec<ParamTy ...>(key_set{binding.key_code, action, mode_bits}, press_duration, std::get<Idx>(context)...);
                        }(std::index_sequence_for<ParamTy...>{});

                        if((rst & binding_return::reset_press_duration) != binding_return{}){
                            press_duration = 0.f;
                        }
                    }
                }
            }

            [[nodiscard]] explicit binding_state(std::pmr::memory_resource* res)
                : bindings(res){
            }
        };

        using sparse_pool = plf::hive<binding_state, std::pmr::polymorphic_allocator<binding_state>> ;

        struct key_index{
            std::underlying_type_t<key> key_code;
            binding_state* reference;
        };


    private:
        template <typename ...Args>
        friend struct input_managerv;

        std::pmr::memory_resource* memory_pool_{std::pmr::new_delete_resource()};
        sparse_pool states_{memory_pool_};
        std::pmr::vector<key_index> keys_{memory_pool_};

    protected:
        std::pmr::vector<inbound_binding> inbounds_{memory_pool_};
        std::array<std::pmr::vector<pos_binding>, std::to_underlying(pos_binding_target::COUNT)> pos_bindingses_{[this]{
            std::array<std::pmr::vector<pos_binding>, std::to_underlying(pos_binding_target::COUNT)> rst{};
            for (auto && pos_bindings : rst){
                pos_bindings = std::pmr::vector<pos_binding>(memory_pool_);
            }
            return rst;
        }()};

    private:

        bool activated_{true};
        mode current_mode_{mode::none};

    public:

        using referenced_object::ref_decr;
        using referenced_object::ref_incr;

        [[nodiscard]] key_mapping_interface() = default;

        [[nodiscard]] explicit key_mapping_interface(std::pmr::memory_resource* memory_pool)
            : memory_pool_(memory_pool){
        }

        virtual ~key_mapping_interface() = default;

        void update(const float delta_in_tick){
            if(!activated_) return;

            update_impl(delta_in_tick);
        }

        void set_activated(const bool active) noexcept{
            activated_ = active;
            if(!active){
                current_mode_ = {};
                reset_key_states();
            }
        }

        [[nodiscard]] bool is_activated() const noexcept{
            return activated_;
        }

        void activate() noexcept{
            activated_ = true;
        }

        void deactivate() noexcept{
            activated_ = false;
            current_mode_ = {};
            reset_key_states();
        }

    private:
        void reset_key_states() noexcept{
            for (binding_state& state : states_){
                state.double_click_timer = 0;
                state.press_duration = 0;
                state.action_state = act_state::released;
            }
        }

        template <typename S>
        copy_const_t<S, binding_state>* find_state_of(this S& self, std::uint16_t idx) noexcept{
            if(std::ranges::size(self.keys_) < 32){
                auto where = std::ranges::find(self.keys_, idx, &key_index::key_code);
                if(where == self.keys_.end())return nullptr;

                return where->reference;
            }else{
                auto where = std::ranges::lower_bound(self.keys_, idx, {}, &key_index::key_code);
                if(where == self.keys_.end() || where->key_code != idx)return nullptr;

                return where->reference;
            }
        }

        void update_mode(const key_set k) noexcept {
            current_mode_ = k.mode_bits;
        }

        [[nodiscard]] bool check_pressed_impl(std::uint16_t keyCode) const noexcept{
            auto* s = find_state_of(keyCode);
            if(!s)return false;

            return s->action_state != act_state::released;
        }

    protected:
        // 参数类型改为 any_key_binding
        bool add_raw(any_key_binding erased_binding){
            const auto idx = erased_binding.key_code;
            auto where = std::ranges::lower_bound(keys_, idx, {}, &key_index::key_code);

            binding_state* state;
            if(where == keys_.end() || where->key_code != idx){
                const auto itr = states_.insert(binding_state{memory_pool_});
                state = std::to_address(itr);
                keys_.insert(where, {idx, state});
            }else{
                state = where->reference;
            }

            if(const auto itr = std::ranges::find(state->bindings, erased_binding); itr != state->bindings.end()){
                return false;
            }

            state->bindings.push_back(static_cast<bind_type>(erased_binding));
            return true;
        }

        virtual void trigger(binding_state& state, act act, mode mode) = 0;
        virtual void trigger(pos_binding_target target, math::vec2) = 0;
        virtual void trigger(bool inbound, math::vec2) = 0;

    public:
        // 参数类型改为 any_key_binding
        bool erase_binding(any_key_binding erased_binding) noexcept{
            const auto idx = erased_binding.key_code;
            auto where = std::ranges::lower_bound(keys_, idx, {}, &key_index::key_code);

            if(where == keys_.end() || where->key_code != idx)return false;

            if(const auto itr = std::ranges::find(where->reference->bindings, erased_binding); itr == where->reference->bindings.end()){
                return false;
            }else{
                where->reference->bindings.erase(itr);
                if(!where->reference->bindings.empty())return true;

                const auto state_itr = states_.get_iterator(where->reference);
                assert(state_itr != states_.end());
                states_.erase(state_itr);
                keys_.erase(where);
                return true;
            }
        }

        bool erase_pos_binding(const pos_binding_target target, const pos_binding binding) noexcept{
            auto& slot = pos_bindingses_[std::to_underlying(target)];
            return std::erase(slot, binding);
        }

        bool erase_inbound_binding(const inbound_binding binding) noexcept{
            return std::erase(inbounds_, binding);
        }

        [[nodiscard]] bool check_pressed(key k) const noexcept{
            return check_pressed_impl(std::to_underlying(k));
        }

        [[nodiscard]] bool check_pressed(mouse k) const noexcept{
            return check_pressed_impl(std::to_underlying(k));
        }

        void inform_input(const key_set k){
            if(!activated_)return;
            update_mode(k);
            inform_input_impl(k);
        }

        void inform_input(pos_binding_target target, math::vec2 pos){
            if(!activated_)return;
            trigger(target, pos);
        }

        void inform_inbound(bool inbounded, math::vec2 pos){
            if(!activated_)return;
            trigger(inbounded, pos);
        }

        [[nodiscard]] mode get_mode() const noexcept{
            return current_mode_;
        }

        [[nodiscard]] bool has_mode(const mode mode) const noexcept{
            return (get_mode() & mode) == mode;
        }


        key_mapping_interface(const key_mapping_interface& other) = delete;
        key_mapping_interface(key_mapping_interface&& other) noexcept = default;
        key_mapping_interface& operator=(const key_mapping_interface& other) = delete;
        key_mapping_interface& operator=(key_mapping_interface&& other) noexcept = default;
    private:
        void update_impl(float delta_in_tick){
            for (binding_state& state : states_){
                if(state.double_click_timer > 0.f){
                    state.double_click_timer -= delta_in_tick;
                }

                if(state.action_state != act_state::released){
                    state.press_duration += delta_in_tick;
                    trigger(state, act::continuous, get_mode());
                }
            }
        }

        void inform_input_impl(key_set key){
            const auto s = find_state_of(key.key_code);
            if(!s)return;

            binding_state& state = *s;

            switch(key.action){
            case act::release:
                state.double_click_timer = 0;
                state.action_state = act_state::released;
                trigger(state, act::release, get_mode());
                break;
            case act::press:
                if(state.double_click_timer > 0){
                    trigger(state, act::double_press, get_mode());
                    state.double_click_timer = 0.f;
                }else{
                    state.double_click_timer = 30.f;
                }

                state.action_state = act_state::pressed;
                trigger(state, act::press, get_mode());

                break;
            case act::repeat:
                state.action_state = act_state::repeating;
                trigger(state, act::repeat, get_mode());

                break;
            default: break;
            }

        }
    };

    export
    template <typename... ParamTy>
    class key_mapping : public key_mapping_interface{
        std::pmr::memory_resource* memory_pool_{std::pmr::new_delete_resource()};

    public:
        using bind_type_with_args = key_binding<ParamTy...>;
        using context_tuple_t = refable_tuple<ParamTy ...>;

    private:
        context_tuple_t context_{};
    public:
        [[nodiscard]] key_mapping() = default;

        [[nodiscard]] explicit key_mapping(std::pmr::memory_resource* memory_pool)
            : key_mapping_interface(memory_pool){
        }

        void trigger(binding_state& state, act act, mode mode) override{
            state.trigger<ParamTy...>(context_, act, mode);
        }

    private:
        void trigger(pos_binding_target target, math::vec2 pos) override{
            for (const auto & bind : pos_bindingses_[std::to_underlying(target)]){
                [&, this]<std::size_t ...Idx>(std::index_sequence<Idx...>){
                    bind.template cast_and_exec<ParamTy...>(pos, static_cast<ParamTy>(std::get<Idx>(context_)) ...);
                }(std::index_sequence_for<ParamTy...>{});
            }
        }

        void trigger(bool inbounded, math::vec2 pos) override{
            for (const auto & bind : inbounds_){
                [&, this]<std::size_t ...Idx>(std::index_sequence<Idx...>){
                    bind.template cast_and_exec<ParamTy...>(inbounded, pos, static_cast<ParamTy>(std::get<Idx>(context_)) ...);
                }(std::index_sequence_for<ParamTy...>{});
            }
        }

    public:
        template <typename Fn>
            requires requires(Fn fn){
                +mo_yanxi::func_take_any_params<typename bind_type_with_args::signature>(fn);
            }
        bool add_binding(key_set key, Fn fn){
            return this->add_raw(bind_type_with_args{key, +mo_yanxi::func_take_any_params<typename bind_type_with_args::signature>(fn)});
        }

        bool add_binding(key_set key, bind_type_with_args::function_ptr fn){
            return add_raw(bind_type_with_args{key, fn});
        }
        template <typename Fn>
        bool add_binding(pos_binding_target target, Fn fn){
            return this->add_binding(target, +mo_yanxi::func_take_any_params<typename pos_binding::function_ptr<ParamTy ...>>(fn));
        }

        bool add_binding(pos_binding_target target, typename pos_binding::function_ptr<ParamTy ...> pfn){
            auto& rng = pos_bindingses_[std::to_underlying(target)];
            if(std::ranges::contains(rng, pfn))return false;
            rng.emplace_back(pfn);
            return true;
        }

        template <typename Fn>
        bool add_inbound_binding(Fn fn){
            return this->add_inbound_binding(+mo_yanxi::func_take_any_params<typename inbound_binding::function_ptr<ParamTy ...>>(fn));
        }

        bool add_inbound_binding(typename inbound_binding::function_ptr<ParamTy ...> pfn){
            if(std::ranges::contains(inbounds_, pfn))return false;
            inbounds_.emplace_back(pfn);
            return true;
        }

        bool add_binding(bind_type_with_args bindging){
            return this->add_raw(bindging);
        }

        [[nodiscard]] const context_tuple_t& get_context() const{
            return context_;
        }

        template <typename ...Args>
            requires (requires(context_tuple_t& ctx, Args&& ...args){
            ctx = mo_yanxi::make_refable_tuple(args...);
            })
        void set_context(Args&& ...param_ty){
            context_ = mo_yanxi::make_refable_tuple(param_ty...);
        }

        void set_context(const context_tuple_t& param_ty){
            context_ = param_ty;
        }

        void set_context(context_tuple_t&& param_ty){
            context_ = std::move(param_ty);
        }

        template <std::size_t Idx, typename Arg>
            requires (std::assignable_from<std::add_lvalue_reference_t<std::tuple_element_t<Idx, context_tuple_t>>, Arg>)
        void set_context_at(Arg&& param_ty) requires (sizeof...(ParamTy) > 1){
            std::get<Idx>(context_) = std::forward<Arg>(param_ty);
        }
    };
}