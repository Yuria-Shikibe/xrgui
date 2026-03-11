with open('src/gui/core/elements/gui.slider_logic.ixx', 'r') as f:
    text = f.read()

# Let's see the clamp logic.
# `std::clamp(..., static_cast<ValueType>(0), static_cast<ValueType>(1))`
# For integral, it should clamp to `segments` if `segments` > 0, otherwise what?
# "when using intgeral, segments is the max value"
# So for integral, the maximum value is `segments`.
# The minimum value is 0.

# Let's re-write `slider_slot` methods using `if constexpr (std::floating_point<ValueType>)` and `if constexpr (std::integral<ValueType>)`.
