with open('src/gui/core/elements/gui.slider_logic.ixx', 'r') as f:
    text = f.read()

# I will replace `slider_slot` definition with the one taking `if constexpr (std::floating_point<ValueType>)` and `if constexpr (std::integral<ValueType>)`.
