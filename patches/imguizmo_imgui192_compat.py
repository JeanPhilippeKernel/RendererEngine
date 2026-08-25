#!/usr/bin/env python3
# Patches ImGuizmo 1.83 for compatibility with Dear ImGui >= 1.92.
# Usage: python3 imguizmo_imgui192_compat.py <path/to/ImGuizmo.cpp>
#
# Changes:
#   - ImGui::CaptureMouseFromApp() removed in 1.90 -> SetNextFrameWantCaptureMouse(true)
#   - ImDrawList::AddPolyline() signature changed in 1.92.8: thickness and flags swapped,
#     and the old bool 'closed' parameter became ImDrawFlags_Closed.

import sys
import os

if len(sys.argv) < 2:
    print("Usage: imguizmo_imgui192_compat.py <path/to/ImGuizmo.cpp>")
    sys.exit(1)

path = sys.argv[1]
if not os.path.exists(path):
    print(f"File not found: {path}")
    sys.exit(1)

with open(path, 'r') as f:
    text = f.read()

original = text

# Fix 1: CaptureMouseFromApp removed in 1.90
text = text.replace(
    'ImGui::CaptureMouseFromApp()',
    'ImGui::SetNextFrameWantCaptureMouse(true)'
)

# Fix 2: AddPolyline signature — (points, count, col, closed_bool, thickness)
#                             -> (points, count, col, thickness, flags)
text = text.replace(
    'drawList->AddPolyline(circlePos, circleMul * halfCircleSegmentCount + 1, colors[3 - axis], false, 2);',
    'drawList->AddPolyline(circlePos, circleMul * halfCircleSegmentCount + 1, colors[3 - axis], 2.f, 0);'
)
text = text.replace(
    'drawList->AddPolyline(circlePos, halfCircleSegmentCount, IM_COL32(0xFF, 0x80, 0x10, 0xFF), true, 2);',
    'drawList->AddPolyline(circlePos, halfCircleSegmentCount, IM_COL32(0xFF, 0x80, 0x10, 0xFF), 2.f, ImDrawFlags_Closed);'
)
text = text.replace(
    'drawList->AddPolyline(screenQuadPts, 4, directionColor[i], true, 1.0f);',
    'drawList->AddPolyline(screenQuadPts, 4, directionColor[i], 1.0f, ImDrawFlags_Closed);'
)

if text == original:
    print("ImGuizmo: already patched, nothing to do.")
else:
    with open(path, 'w') as f:
        f.write(text)
    print("ImGuizmo: patched for ImGui 1.92 compatibility.")
