with open('src/gui/core/elements/gui.slider_logic.ixx', 'r') as f:
    text = f.read()

# Let's verify what `is_segment_move_activated` does for floating point vs integral.
import re
print("Checking is_segment_move_activated...")
lines = text.split('\n')
for i, line in enumerate(lines):
    if 'is_segment_move_activated' in line:
        print(f"Line {i}: {line}")
