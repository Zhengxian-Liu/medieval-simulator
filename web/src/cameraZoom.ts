// Camera pose for one zoom level. t=0: near-orthographic isometric overview; t=1: mid-range view of the character.
// Distance is derived from FOV so the framed area shrinks smoothly while the FOV opens (no pop).

export interface CameraPose {
  distance: number; // metres from the focus point
  fovDeg: number; // vertical
  pitchDeg: number; // degrees below the horizon
  yawDeg: number;
}

export const FAR = { fovDeg: 12, framedWidth: 40, pitchDeg: 48, yawDeg: 45 } as const;
export const NEAR = { fovDeg: 35, framedWidth: 7, pitchDeg: 32 } as const;

const ASPECT = 16 / 9; // framing is specified as horizontal width at this aspect

export function framedWidth(pose: CameraPose): number {
  return 2 * pose.distance * Math.tan(((pose.fovDeg * Math.PI) / 180) / 2) * ASPECT;
}

export function evaluateCameraZoom(t: number): CameraPose {
  t = Math.min(1, Math.max(0, t));
  const ease = t * t * (3 - 2 * t);
  const fovDeg = FAR.fovDeg + (NEAR.fovDeg - FAR.fovDeg) * ease;
  // Geometric interpolation: each wheel notch zooms by the same ratio.
  const width = FAR.framedWidth * Math.pow(NEAR.framedWidth / FAR.framedWidth, t);
  const distance = width / (2 * Math.tan(((fovDeg * Math.PI) / 180) / 2) * ASPECT);
  return {
    distance,
    fovDeg,
    pitchDeg: FAR.pitchDeg + (NEAR.pitchDeg - FAR.pitchDeg) * ease,
    yawDeg: FAR.yawDeg,
  };
}
