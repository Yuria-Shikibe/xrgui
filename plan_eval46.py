# The user wants me to use `if constexpr` to distinguish between floating point and integral logic in `slider_slot`.
# For floating point: `segments` is the number of segments (or 0 for continuous).
# For integral: `segments` is the max value.

# Wait, if `ValueType` is integral, then the progress value itself goes from 0 to `segments`.
# But `bar_progress_` is a `snap_shot<ValueType>`.
# Does it mean `progress` goes from 0 to `segments` instead of 0 to 1?
# Let's read `slider_logic.ixx` and see where we clamp to 1.
