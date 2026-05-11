// Shared types for the naina-web preview. API surface mirrors the
// planned naina.Engine for Python/Node so the demo is a "would-be" client.

export interface BBox { x: number; y: number; w: number; h: number; score: number; }
export interface Point { x: number; y: number; }

export interface Face {
  bbox: BBox;
  landmarks: [Point, Point, Point, Point, Point];   // L-eye, R-eye, nose, L-mouth, R-mouth
  quality: number;        // 0..1
  trackId: number;        // -1 when not tracked
}

export interface EnrolledIdentity {
  id: string;
  name: string;
  embedding: Float32Array;   // L2-normalized
  enrolledAt: number;
}

export interface RecognitionMatch {
  identityId: string | null;
  name: string;
  similarity: number;        // best cosine similarity, or 0 if no gallery
}

export interface FrameResult {
  faces: Face[];
  matches: RecognitionMatch[];      // index-aligned with faces
  timings: { detectMs: number; embedMsPerFace: number };
}

export interface EngineOptions {
  modelBase?: string;          // root URL for model files (default: "./models")
  detectorInputSize?: number;  // YuNet square input (default 320; 640 for higher accuracy)
  confidenceThreshold?: number;
  nmsIouThreshold?: number;
  topK?: number;               // YuNet pre-NMS top-K
}
