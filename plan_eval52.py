# In `move_minimum_delta`:
# if(is_segment_move_activated()){
#     ValueType sign = move > static_cast<ValueType>(0) ? static_cast<ValueType>(1) : (move < static_cast<ValueType>(0) ? static_cast<ValueType>(-1) : static_cast<ValueType>(0));
#     move_progress(sign * get_segment_unit(), base_ptr);
# } else{
#     move_progress(move, base_ptr);
# }
# This logic applies for both float and integral because `get_segment_unit` is 1 for integral and `is_segment_move_activated` is true for integral. So it moves by 1.

# What about `move_progress` with one argument?
# It calls `move_progress(movement, bar_progress_.*base_ptr)`
# Inside `move_progress(movement, base)`:
# if constexpr(std::floating_point<ValueType>){
#     if(is_segment_move_activated()){
#         bar_progress_.temp = std::clamp(static_cast<ValueType>(std::round((base + movement) / get_segment_unit()) * get_segment_unit()), static_cast<ValueType>(0), get_max_value());
#     } else{
#         bar_progress_.temp = std::clamp(static_cast<ValueType>(base + movement), static_cast<ValueType>(0), get_max_value());
#     }
# } else if constexpr(std::integral<ValueType>){
#     bar_progress_.temp = std::clamp(static_cast<ValueType>(base + movement), static_cast<ValueType>(0), get_max_value());
# }

# This all looks solid and properly applies `if constexpr` to divide float/integral behavior seamlessly.
