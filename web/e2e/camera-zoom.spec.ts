import { expect, test } from '@playwright/test';
import { FAR, NEAR, evaluateCameraZoom, framedWidth } from '../src/cameraZoom';

// Failure modes:
// 1. Out-of-range zoom produces a pose outside the far..near envelope.
// 2. Far end loses the near-orthographic isometric look (wide FOV, wrong pitch, wrong framing).
// 3. Near end is not a mid-range character view (narrow FOV, camera far from the character).
// 4. Zooming in moves the camera away or widens the framed area at some step (visible pop).

test('clamps input', () => {
  expect(evaluateCameraZoom(-3)).toEqual(evaluateCameraZoom(0));
  expect(evaluateCameraZoom(7)).toEqual(evaluateCameraZoom(1));
});

test('far end is isometric', () => {
  const far = evaluateCameraZoom(0);
  expect(far.fovDeg).toBeLessThanOrEqual(15);
  expect(far.pitchDeg).toBe(FAR.pitchDeg);
  expect(far.yawDeg).toBe(FAR.yawDeg);
  expect(framedWidth(far)).toBeCloseTo(FAR.framedWidth, 3);
});

test('near end frames the character at mid range', () => {
  const near = evaluateCameraZoom(1);
  expect(near.fovDeg).toBeGreaterThanOrEqual(30);
  expect(near.distance).toBeLessThanOrEqual(12);
  expect(framedWidth(near)).toBeCloseTo(NEAR.framedWidth, 3);
});

test('zoom is monotonic', () => {
  let prev = evaluateCameraZoom(0);
  for (let step = 1; step <= 200; step++) {
    const cur = evaluateCameraZoom(step / 200);
    expect(cur.distance).toBeLessThanOrEqual(prev.distance);
    expect(framedWidth(cur)).toBeLessThanOrEqual(framedWidth(prev));
    prev = cur;
  }
});
