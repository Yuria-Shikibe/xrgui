export module mo_yanxi.graphic.camera;

import mo_yanxi.math;
import mo_yanxi.math.rect_ortho;
import mo_yanxi.math.vector2;
import mo_yanxi.math.matrix3;
import std;

namespace mo_yanxi::graphic {

    // TODO 3D support maybe in the future?
    export
    class camera2 {
    public:
        static constexpr float DefMaximumScale = 5.0f;
        static constexpr float DefMinimumScale = 0.5f;
        static constexpr float DefScaleSpeed = 0.095f;

    protected:
        bool changed{true};

        math::mat3 world_to_clip{};
        math::mat3 clip_to_world{};

        math::vec2 screenSize{};
        math::vec2 stablePos{};
        math::frect viewport{};
        math::frect lastViewport{};

        // 内部存储直接使用线性缩放值，不再存储对数值
        float minScale{DefMinimumScale};
        float maxScale{DefMaximumScale};

        float scale{1.0f};
        float targetScale{1.0f};

        constexpr void updateViewportRect() noexcept {
            viewport.set_size(screenSize.x / scale, screenSize.y / scale);
            viewport.set_center(stablePos);
        }

    public:
        float speed_scale{1.0f};
        float clip_margin{50.0f};

        camera2() = default;

        void resume_speed() noexcept { speed_scale = 1.0f; }

        void lock() noexcept { speed_scale = 0.0f; }

        void set_scale_range(const math::range range = {DefMinimumScale, DefMaximumScale}) noexcept {
            auto [min, max] = range.get_ordered();
            minScale = min;
            maxScale = max;
            clamp_target_scale();
        }

        void clamp_target_scale() noexcept {
            targetScale = std::clamp(targetScale, minScale, maxScale);
        }

        void resize_screen(const float w, const float h) noexcept {
            screenSize.set(w, h);
            changed = true;
        }

        [[nodiscard]] constexpr math::frect get_viewport() const noexcept { return viewport; }

        constexpr void clamp_position(const math::frect& region) noexcept {
            stablePos.clamp_xy(region.get_src(), region.get_end());
        }

        constexpr void move(const float x, const float y) noexcept {
            stablePos += math::vec2{x, y} * speed_scale;
        }

        constexpr void move(const math::vec2 vec) noexcept {
            stablePos += vec * speed_scale;
        }

        [[nodiscard]] constexpr math::mat3 get_v2v_mat(math::vec2 offset) const noexcept {
            return math::mat3{}.set_rect_transform(viewport.src, viewport.extent(), offset, screenSize);
        }

        [[nodiscard]] constexpr math::vec2 get_stable_center() const noexcept { return stablePos; }

        [[nodiscard]] constexpr math::vec2 get_viewport_center() const noexcept { return viewport.get_center(); }

        [[nodiscard]] constexpr math::frect get_clip_space() const noexcept { return viewport.copy().expand(clip_margin, clip_margin); }

        constexpr void set_center(const math::vec2 pos) noexcept { stablePos = pos; }

        [[nodiscard]] constexpr math::vec2 get_screen_center() const noexcept { return screenSize / 2.0f; }

        template <typename T>
        [[nodiscard]] T map_scale(const T& src, const T& dst) const noexcept {
            // 在对数空间进行映射，以保证视觉感知的均匀性
            return math::map(std::log(scale), std::log(minScale), std::log(maxScale), src, dst);
        }

        void update(const float delta) {
            // 使用对数空间进行平滑插值，使得缩放在视觉上是匀速且平滑的
            const float logScale = std::log(scale);
            const float logTarget = std::log(targetScale);

            scale = std::exp(std::lerp(logScale, logTarget, delta * DefScaleSpeed));

            if (std::abs(scale - targetScale) < 0.00025f) {
                scale = targetScale;
            }

            updateViewportRect();

            if (viewport != lastViewport || changed) {
                changed = true;

                // 标准 Y-down 的 Clip Space 映射（例如 Vulkan），将左上角映射到 (-1, -1)，右下角映射到 (1, 1)
                world_to_clip.set_orthogonal(viewport.get_src(), viewport.extent());
                clip_to_world.set(world_to_clip).inv();
            }

            lastViewport = viewport;
        }

        bool check_changed() noexcept { return std::exchange(changed, false); }

        [[nodiscard]] const math::mat3& get_world_to_clip() const noexcept { return world_to_clip; }

        [[nodiscard]] const math::mat3& get_clip_to_world() const noexcept { return clip_to_world; }

        [[nodiscard]] float get_scale() const noexcept { return scale; }

        void set_scale(const float s) noexcept {
            scale = std::clamp(s, minScale, maxScale);
        }

        [[nodiscard]] float get_target_scale() const noexcept { return targetScale; }

        void set_scale_by_delta(const float delta) noexcept {
            // 滚轮缩放累加 delta 等价于在对数空间相加，对应到线性空间是乘法
            // 例如 delta = 0.1 相当于乘以 e^0.1，实现完美的视觉均匀缩放
            set_target_scale(targetScale * std::exp(delta));
        }

        void set_target_scale(const float target) noexcept {
            targetScale = std::clamp(target, minScale, maxScale);
        }

        void set_target_scale_def() noexcept {
            targetScale = get_target_scale_def();
        }

        [[nodiscard]] float get_target_scale_def() const noexcept {
            return std::clamp(1.0f, minScale, maxScale);
        }

        [[nodiscard]] math::vec2 get_screen_size() const noexcept { return screenSize; }

        // World -> Screen 转换（Y-Down，屏幕左上角为 (0,0)）
        [[nodiscard]] math::vec2 get_world_to_screen(const math::vec2 inWorld) const noexcept {
            return (inWorld - stablePos) * scale + (screenSize / 2.0f);
        }

        // Screen -> World 转换
        [[nodiscard]] math::vec2 get_screen_to_world(const math::vec2 inScreen, const math::vec2 offset = {}) const noexcept {
            return (inScreen - offset - (screenSize / 2.0f)) / scale + stablePos;
        }
    };
}