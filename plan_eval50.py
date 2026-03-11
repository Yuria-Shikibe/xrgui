with open('src/gui/core/elements/gui.slider_logic.ixx', 'r') as f:
    text = f.read()

# Let's verify `std::isnan` is guarded for floating point.
import re
print("Checking isnan usage...")
lines = text.split('\n')
for i, line in enumerate(lines):
    if 'std::isnan' in line:
        print(f"Line {i}: {line}")
