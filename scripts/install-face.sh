#!/bin/bash
# Install face recognition dependencies on Jetson
# Run: bash scripts/install-face.sh
set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
echo "=== Installing Face Recognition Dependencies ==="

cd "$PROJECT_DIR"
source venv/bin/activate

# Core dependencies
pip install insightface opencv-python aiohttp

# ONNX Runtime - try GPU first, fallback to CPU
pip install onnxruntime-gpu 2>/dev/null || pip install onnxruntime
echo "ONNX Runtime installed (check GPU support with: python -c 'import onnxruntime; print(onnxruntime.get_available_providers())')"

# Download InsightFace model (buffalo_s) on first run
python -c "
from insightface.app import FaceAnalysis
app = FaceAnalysis(name='buffalo_s', providers=['CPUExecutionProvider'])
app.prepare(ctx_id=0, det_size=(640, 480))
print('InsightFace buffalo_s model downloaded and ready')
"

echo "=== Face Recognition Installation Complete ==="
