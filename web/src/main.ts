import * as THREE from 'three';
import { EffectComposer } from 'three/examples/jsm/postprocessing/EffectComposer.js';
import { GTAOPass } from 'three/examples/jsm/postprocessing/GTAOPass.js';
import { OutputPass } from 'three/examples/jsm/postprocessing/OutputPass.js';
import { RenderPass } from 'three/examples/jsm/postprocessing/RenderPass.js';
import { evaluateCameraZoom } from './cameraZoom';
import { buildVillage } from './village';

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
renderer.setSize(innerWidth, innerHeight);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.info.autoReset = false; // count the whole frame, not just the last composer pass
document.body.appendChild(renderer.domElement);

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(30, innerWidth / innerHeight, 0.1, 2000);
const { player } = buildVillage(scene);

const composer = new EffectComposer(renderer);
composer.addPass(new RenderPass(scene, camera));
const ao = new GTAOPass(scene, camera, innerWidth, innerHeight);
ao.updateGtaoMaterial({ radius: 0.6, scale: 1.2 });
composer.addPass(ao);
composer.addPass(new OutputPass());

let zoom = 0;
let zoomTarget = 0;
const ZOOM_STEP = 0.08;
const ZOOM_SPEED = 6;
const MOVE_SPEED = 4; // m/s

function applyCamera() {
  const pose = evaluateCameraZoom(zoom);
  const pitch = THREE.MathUtils.degToRad(pose.pitchDeg);
  const yaw = THREE.MathUtils.degToRad(pose.yawDeg);
  const focus = player.position.clone().setY(1);
  camera.position.set(
    focus.x - Math.cos(pitch) * Math.sin(yaw) * pose.distance,
    focus.y + Math.sin(pitch) * pose.distance,
    focus.z + Math.cos(pitch) * Math.cos(yaw) * pose.distance,
  );
  camera.fov = pose.fovDeg;
  camera.updateProjectionMatrix();
  camera.lookAt(focus);
}

addEventListener('wheel', (e) => {
  zoomTarget = THREE.MathUtils.clamp(zoomTarget + (e.deltaY < 0 ? ZOOM_STEP : -ZOOM_STEP), 0, 1);
});

const keys = new Set<string>();
addEventListener('keydown', (e) => keys.add(e.code));
addEventListener('keyup', (e) => keys.delete(e.code));

addEventListener('resize', () => {
  camera.aspect = innerWidth / innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight);
  composer.setSize(innerWidth, innerHeight);
});

// Screen-relative movement: "up" walks away from the camera along the iso yaw.
function move(dt: number) {
  const yaw = THREE.MathUtils.degToRad(evaluateCameraZoom(zoom).yawDeg);
  const forward = new THREE.Vector3(Math.sin(yaw), 0, -Math.cos(yaw));
  const right = new THREE.Vector3(Math.cos(yaw), 0, Math.sin(yaw));
  const dir = new THREE.Vector3();
  if (keys.has('KeyW')) dir.add(forward);
  if (keys.has('KeyS')) dir.sub(forward);
  if (keys.has('KeyD')) dir.add(right);
  if (keys.has('KeyA')) dir.sub(right);
  if (dir.lengthSq() > 0) player.position.addScaledVector(dir.normalize(), MOVE_SPEED * dt);
}

const clock = new THREE.Clock();
function frame() {
  const dt = Math.min(clock.getDelta(), 0.1);
  zoom += (zoomTarget - zoom) * Math.min(1, dt * ZOOM_SPEED);
  move(dt);
  applyCamera();
  renderer.info.reset();
  composer.render();
  requestAnimationFrame(frame);
}
frame();

// Test hook for E2E: jump the zoom and report what was drawn.
declare global {
  interface Window {
    __game: { setZoom(t: number): void; zoom(): number; triangles(): number };
  }
}
window.__game = {
  setZoom(t) {
    zoom = zoomTarget = THREE.MathUtils.clamp(t, 0, 1);
  },
  zoom: () => zoom,
  triangles: () => renderer.info.render.triangles,
};
