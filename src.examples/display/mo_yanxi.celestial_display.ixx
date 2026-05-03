module;

export module mo_yanxi.celestial_display;

export import mo_yanxi.graphic.color;
import std;
import mo_yanxi.math.vector2;
import mo_yanxi.math;
import mo_yanxi.graphic.trail;


namespace celestial {

    export constexpr std::uint32_t invalid_parent = std::numeric_limits<std::uint32_t>::max();

    export struct body_definition {
        std::uint32_t parent_index = invalid_parent;
        float semi_major_axis = 0.0f;
        float semi_minor_axis = 0.0f;
        float revolution_period = 1.0f;
        float rotation_period = 1.0f;
        float initial_rev_phase = 0.0f;
        float initial_rot_phase = 0.0f;

        // --- 新增：视觉与轨迹属性 ---
        mo_yanxi::graphic::color render_color = mo_yanxi::graphic::colors::white;
        std::size_t trail_length = 200;  // 尾迹保留的节点数
        float trail_min_dst = 1.0f;      // 尾迹点采样的最小距离，防止移动过慢时堆积
    };

    struct body_constants {
        std::uint32_t parent_index;
        float semi_major_axis;
        float semi_minor_axis;
        float rev_angular_velocity;
        float rot_angular_velocity;
        float initial_rev_phase;
        float initial_rot_phase;
        mo_yanxi::graphic::color color; // 预处理后的超亮颜色
        float trail_min_sqr_dst;            // 预计算距离平方，优化距离判断

    	mo_yanxi::graphic::color get_hdr_color() const noexcept{
    		return color.to_light_by_luma(1.375f);
    	}
    };

    export struct body_state {
        mo_yanxi::math::vec2 global_position;
        float current_rotation;
        mo_yanxi::graphic::trail path_trail; // 管理该星体的轨迹
    };

    export class planetary_system {
    private:
        std::vector<body_constants> constants_;
        std::vector<body_state> states_;

    public:
        planetary_system() = default;

        void reserve(std::size_t capacity) {
            constants_.reserve(capacity);
            states_.reserve(capacity);
        }

        std::uint32_t add_body(const body_definition& def) {
            std::uint32_t new_index = static_cast<std::uint32_t>(constants_.size());

            constexpr float two_pi = 2.0f * std::numbers::pi_v<float>;


            constants_.push_back(body_constants{
                .parent_index = def.parent_index,
                .semi_major_axis = def.semi_major_axis,
                .semi_minor_axis = def.semi_minor_axis,
                .rev_angular_velocity = def.revolution_period != 0.0f ? (two_pi / def.revolution_period) : 0.0f,
                .rot_angular_velocity = def.rotation_period != 0.0f ? (two_pi / def.rotation_period) : 0.0f,
                .initial_rev_phase = def.initial_rev_phase,
                .initial_rot_phase = def.initial_rot_phase,
                .color = def.render_color,
                .trail_min_sqr_dst = def.trail_min_dst * def.trail_min_dst
            });

            // 初始化状态和轨迹队列内存
            states_.push_back(body_state{
                .global_position = mo_yanxi::math::vec2{0.0f, 0.0f},
                .current_rotation = 0.0f,
                .path_trail = mo_yanxi::graphic::trail(def.trail_length)
            });

            return new_index;
        }

        // 每次渲染帧调用
        void update(double global_time) noexcept {
            const std::size_t count = constants_.size();

            for (std::size_t i = 0; i < count; ++i) {
                const auto& constant = constants_[i];
                auto& state = states_[i];

                state.current_rotation = constant.initial_rot_phase + constant.rot_angular_velocity * global_time;

                if (constant.parent_index == invalid_parent) {
                    state.global_position.set_zero();
                } else {
                    float current_rev_angle = constant.initial_rev_phase + constant.rev_angular_velocity * global_time;
                    mo_yanxi::math::vec2 local_pos{
                        constant.semi_major_axis * mo_yanxi::math::cos(current_rev_angle),
                        constant.semi_minor_axis * mo_yanxi::math::sin(current_rev_angle)
                    };
                    state.global_position = states_[constant.parent_index].global_position + local_pos;
                }

                // 更新轨迹 (利用底层 trail 现成的距离剔除逻辑，防止冗余 push)
                if(state.path_trail.capacity() > 2)state.path_trail.push(state.global_position, 1.0f, constant.trail_min_sqr_dst);
            }
        }

        [[nodiscard]] const std::vector<body_state>& get_states() const noexcept { return states_; }
        [[nodiscard]] const std::vector<body_constants>& get_constants() const noexcept { return constants_; }
        [[nodiscard]] std::vector<body_constants>& get_constants() noexcept { return constants_; }
    };
}

export
namespace simulation_data {

    struct body_info {
        std::uint32_t id;
        float render_radius;
        std::string_view name;
        float text_scale;
    };

    std::vector<body_info> populate_solar_system(celestial::planetary_system& system) {
        std::vector<body_info> bodies;
        bodies.reserve(16);
        using namespace mo_yanxi::graphic;

        // 1. 太阳
        std::uint32_t sun_id = system.add_body({
            .parent_index = celestial::invalid_parent,
            .rotation_period = 15.0f,
            .render_color = colors::ENERGY * 1.26f,
            .trail_length = 0
        });
        bodies.push_back({sun_id, 60.0f, "太阳{f:tele}{_}Sun", 2.0f});

        // 2. 水星
        std::uint32_t mercury_id = system.add_body({
            .parent_index = sun_id, .semi_major_axis = 120.0f, .semi_minor_axis = 110.0f,
            .revolution_period = 6.0f, .rotation_period = 8.0f,
            .render_color = colors::gray,
            .trail_length = 40, .trail_min_dst = 4.0f
        });
        bodies.push_back({mercury_id, 8.0f, "水星{f:tele}{_}Mercury", 0.8f});

        // 3. 金星
        std::uint32_t venus_id = system.add_body({
            .parent_index = sun_id, .semi_major_axis = 220.0f, .semi_minor_axis = 220.0f,
            .revolution_period = 15.0f, .rotation_period = -20.0f,
            .render_color = colors::GOLDENROD,
            .trail_length = 60, .trail_min_dst = 5.0f
        });
        bodies.push_back({venus_id, 14.0f, "金星{f:tele}{_}Venus", 1.0f});

        // 4. 地球
        std::uint32_t earth_id = system.add_body({
            .parent_index = sun_id, .semi_major_axis = 350.0f, .semi_minor_axis = 350.0f,
            .revolution_period = 25.0f, .rotation_period = 2.0f,
            .render_color = colors::AQUA_SKY,
            .trail_length = 80, .trail_min_dst = 6.0f
        });
        bodies.push_back({earth_id, 15.0f, "地球{f:tele}{_}Earth", 1.0f});

        // 5. 月球
        std::uint32_t moon_id = system.add_body({
            .parent_index = earth_id, .semi_major_axis = 35.0f, .semi_minor_axis = 35.0f,
            .revolution_period = 2.0f, .rotation_period = 2.0f,
            .render_color = colors::light_gray,
            .trail_length = 25, .trail_min_dst = 1.0f
        });
        bodies.push_back({moon_id, 4.0f, "月球{f:tele}{_}Moon", 0.6f});

        // 6. 火星
        std::uint32_t mars_id = system.add_body({
            .parent_index = sun_id, .semi_major_axis = 500.0f, .semi_minor_axis = 480.0f,
            .revolution_period = 40.0f, .rotation_period = 2.1f,
            .render_color = colors::SCARLET,
            .trail_length = 100, .trail_min_dst = 7.0f
        });
        bodies.push_back({mars_id, 10.0f, "火星{f:tele}{_}Mars", 0.9f});

        // 7. 火卫一
        std::uint32_t phobos_id = system.add_body({
            .parent_index = mars_id, .semi_major_axis = 20.0f, .semi_minor_axis = 18.0f,
            .revolution_period = 0.8f, .render_color = colors::BRICK,
            .trail_length = 25, .trail_min_dst = 0.5f
        });
        bodies.push_back({phobos_id, 2.0f, "火卫一{f:tele}{_}Phobos", 0.5f});

        // 8. 火卫二
        std::uint32_t deimos_id = system.add_body({
            .parent_index = mars_id, .semi_major_axis = 32.0f, .semi_minor_axis = 32.0f,
            .revolution_period = 1.5f, .render_color = colors::BROWN,
            .trail_length = 30, .trail_min_dst = 0.8f
        });
        bodies.push_back({deimos_id, 1.5f, "火卫二{f:tele}{_}Deimos", 0.5f});

        // 9. 木星
        std::uint32_t jupiter_id = system.add_body({
            .parent_index = sun_id, .semi_major_axis = 900.0f, .semi_minor_axis = 880.0f,
            .revolution_period = 65.0f, .rotation_period = 1.2f,
            .render_color = colors::TAN,
            .trail_length = 140, .trail_min_dst = 5.0f
        });
        bodies.push_back({jupiter_id, 40.0f, "木星{f:tele}{_}Jupiter", 1.6f});

        // 10. 木卫一 (Io)
        std::uint32_t io_id = system.add_body({
            .parent_index = jupiter_id, .semi_major_axis = 60.0f, .semi_minor_axis = 60.0f,
            .revolution_period = 1.2f, .render_color = colors::YELLOW,
            .trail_length = 35, .trail_min_dst = 2.0f
        });
        bodies.push_back({io_id, 4.0f, "木卫一{f:tele}{_}Io", 0.6f});

        // 11. 木卫二 (Europa)
        std::uint32_t europa_id = system.add_body({
            .parent_index = jupiter_id, .semi_major_axis = 85.0f, .semi_minor_axis = 85.0f,
            .revolution_period = 2.4f, .render_color = colors::white,
            .trail_length = 45, .trail_min_dst = 2.5f
        });
        bodies.push_back({europa_id, 3.5f, "木卫二{f:tele}{_}Europa", 0.6f});

        // 12. 木卫三 (Ganymede)
        std::uint32_t ganymede_id = system.add_body({
            .parent_index = jupiter_id, .semi_major_axis = 120.0f, .semi_minor_axis = 120.0f,
            .revolution_period = 4.8f, .render_color = colors::gray,
            .trail_length = 55, .trail_min_dst = 3.0f
        });
        bodies.push_back({ganymede_id, 6.0f, "木卫三{f:tele}{_}Ganymede", 0.7f});

        // 13. 木卫四 (Callisto)
        std::uint32_t callisto_id = system.add_body({
            .parent_index = jupiter_id, .semi_major_axis = 160.0f, .semi_minor_axis = 160.0f,
            .revolution_period = 9.6f, .render_color = colors::dark_gray,
            .trail_length = 70, .trail_min_dst = 3.5f
        });
        bodies.push_back({callisto_id, 5.0f, "木卫四{f:tele}{_}Callisto", 0.65f});

        // 14. 土星
        std::uint32_t saturn_id = system.add_body({
            .parent_index = sun_id, .semi_major_axis = 1400.0f, .semi_minor_axis = 1350.0f,
            .revolution_period = 85.0f, .rotation_period = 1.3f,
            .render_color = colors::pale_yellow,
            .trail_length = 180, .trail_min_dst = 6.0f
        });
        bodies.push_back({saturn_id, 32.0f, "土星{f:tele}{_}Saturn", 1.4f});

        // 15. 天王星
        std::uint32_t uranus_id = system.add_body({
            .parent_index = sun_id, .semi_major_axis = 1900.0f, .semi_minor_axis = 1880.0f,
            .revolution_period = 105.0f, .rotation_period = 1.5f,
            .render_color = colors::ROYAL.create_lerp(colors::aqua, .35f),
            .trail_length = 210, .trail_min_dst = 8.0f
        });
        bodies.push_back({uranus_id, 20.0f, "天王星{f:tele}{_}Uranus", 1.15f});

        // 16. 海王星
        std::uint32_t neptune_id = system.add_body({
            .parent_index = sun_id, .semi_major_axis = 2300.0f, .semi_minor_axis = 2280.0f,
            .revolution_period = 120.0f, .rotation_period = 1.4f,
            .render_color = colors::ROYAL.create_lerp(colors::BLUE, .5f),
            .trail_length = 250, .trail_min_dst = 10.0f
        });
        bodies.push_back({neptune_id, 19.0f, "海王星{f:tele}{_}Neptune", 1.1f});

        for (auto && body : system.get_constants()){
            body.rev_angular_velocity *= .05f;
        }

        return bodies;
    }
}