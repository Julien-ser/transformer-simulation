# Transformation status message
string current_form              # "humanoid" or "vehicular"
string transformation_state      # IDLE, TRANSFORMING_TO_VEHICULAR, TRANSFORMING_TO_HUMANOID, EMERGENCY_STOP, ERROR
float64 progress                 # 0.0 to 1.0
bool is_safe                     # true if system is safe
---
# Transformation request (empty, uses current state)
