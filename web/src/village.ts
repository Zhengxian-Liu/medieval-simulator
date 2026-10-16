import * as THREE from 'three';

// Placeholder test village (metres): houses, river, bridge, torch, rocks, trees, daylight.
// Untextured on purpose; Tripo/Blender assets replace these meshes later.

const color = {
  grass: 0x4a5a2a,
  stone: 0x6b6862,
  plaster: 0xcfc2a6,
  timber: 0x4a3220,
  thatch: 0x8a6d3b,
  slate: 0x4e5257,
  water: 0x1f4a52,
  bark: 0x3d2b1c,
  leaves: 0x2f4a22,
  iron: 0x222222,
  cloth: 0x7a2e24,
};

const mat = (c: number, roughness = 0.9) => new THREE.MeshStandardMaterial({ color: c, roughness });

// Deterministic so screenshots are comparable between runs.
function rng(seed: number) {
  return () => {
    seed = (seed * 1664525 + 1013904223) >>> 0;
    return seed / 2 ** 32;
  };
}

function mesh(geo: THREE.BufferGeometry, material: THREE.Material) {
  const m = new THREE.Mesh(geo, material);
  m.castShadow = true;
  m.receiveShadow = true;
  return m;
}

function house(w: number, d: number, h: number, wall: number, roof: number) {
  const g = new THREE.Group();
  const base = mesh(new THREE.BoxGeometry(w + 0.3, 0.6, d + 0.3), mat(color.stone));
  base.position.y = 0.3;
  const body = mesh(new THREE.BoxGeometry(w, h, d), mat(wall));
  body.position.y = 0.6 + h / 2;
  // Closed gable roof: triangular prism along X, with eaves overhang.
  const rise = d * 0.55;
  const tri = new THREE.Shape([
    new THREE.Vector2(-d / 2 - 0.4, 0),
    new THREE.Vector2(d / 2 + 0.4, 0),
    new THREE.Vector2(0, rise),
  ]);
  const roofGeo = new THREE.ExtrudeGeometry(tri, { depth: w + 0.6, bevelEnabled: false });
  roofGeo.translate(0, 0, -(w + 0.6) / 2);
  roofGeo.rotateY(Math.PI / 2);
  const roofMesh = mesh(roofGeo, mat(roof));
  roofMesh.position.y = 0.6 + h;
  const door = mesh(new THREE.BoxGeometry(0.9, 1.8, 0.08), mat(color.timber));
  door.position.set(0, 0.6 + 0.9, d / 2 + 0.04);
  g.add(base, body, roofMesh, door);
  return g;
}

function tree(r: () => number) {
  const g = new THREE.Group();
  const h = 3 + r() * 3;
  const trunk = mesh(new THREE.CylinderGeometry(0.15, 0.22, h * 0.4, 6), mat(color.bark));
  trunk.position.y = h * 0.2;
  const crown = mesh(new THREE.ConeGeometry(1 + r() * 0.6, h * 0.8, 7), mat(color.leaves));
  crown.position.y = h * 0.4 + h * 0.4;
  g.add(trunk, crown);
  return g;
}

function rock(r: () => number) {
  const geo = new THREE.DodecahedronGeometry(0.3 + r() * 0.7, 0);
  const pos = geo.attributes.position;
  for (let i = 0; i < pos.count; i++) pos.setY(i, pos.getY(i) * 0.6);
  geo.computeVertexNormals();
  return mesh(geo, mat(color.stone));
}

export function buildVillage(scene: THREE.Scene) {
  const r = rng(7);
  scene.background = new THREE.Color(0xa9c2d6);
  scene.fog = new THREE.Fog(0xa9c2d6, 120, 400);

  scene.add(new THREE.HemisphereLight(0xc8dcf0, 0x4a4030, 1.2));
  const sun = new THREE.DirectionalLight(0xfff1dc, 3.2);
  sun.position.set(-30, 45, 20);
  sun.castShadow = true;
  sun.shadow.mapSize.set(4096, 4096);
  sun.shadow.camera.left = sun.shadow.camera.bottom = -45;
  sun.shadow.camera.right = sun.shadow.camera.top = 45;
  sun.shadow.bias = -0.0004;
  sun.shadow.normalBias = 0.02;
  scene.add(sun);

  const ground = mesh(new THREE.PlaneGeometry(240, 240), mat(color.grass, 1));
  ground.rotation.x = -Math.PI / 2;
  scene.add(ground);

  // River along X, slightly sunk, with a stone bank strip each side.
  const river = new THREE.Group();
  const water = new THREE.Mesh(
    new THREE.PlaneGeometry(240, 7),
    new THREE.MeshStandardMaterial({ color: color.water, roughness: 0.08, metalness: 0.1 }),
  );
  water.rotation.x = -Math.PI / 2;
  water.position.y = 0.02;
  water.receiveShadow = true;
  river.add(water);
  for (const side of [-1, 1]) {
    const bank = mesh(new THREE.BoxGeometry(240, 0.2, 0.8), mat(color.stone));
    bank.position.set(0, 0.05, side * 3.8);
    river.add(bank);
  }
  river.position.z = -14;
  river.rotation.y = THREE.MathUtils.degToRad(8);
  scene.add(river);

  const bridge = mesh(new THREE.BoxGeometry(2.5, 0.25, 10), mat(color.timber));
  bridge.position.set(3, 0.45, -14.4);
  bridge.rotation.y = THREE.MathUtils.degToRad(8);
  scene.add(bridge);

  const houses: [number, number, number, number, number][] = [
    // x, z, yawDeg, wall, roof
    [-9, 6, 10, color.plaster, color.thatch],
    [6, 9, -15, color.timber, color.thatch],
    [-3, 19, 80, color.plaster, color.slate],
    [15, -3, 40, color.plaster, color.thatch],
  ];
  for (const [x, z, yaw, wall, roof] of houses) {
    const h = house(6, 4.5, 3.2, wall, roof);
    h.position.set(x, 0, z);
    h.rotation.y = THREE.MathUtils.degToRad(yaw);
    scene.add(h);
  }

  // Props near the square: barrels and a market cloth stall.
  for (let i = 0; i < 5; i++) {
    const barrel = mesh(new THREE.CylinderGeometry(0.32, 0.35, 0.9, 10), mat(color.timber));
    barrel.position.set(-4 + (i % 3) * 0.75, 0.45, 9.5 + Math.floor(i / 3) * 0.75);
    scene.add(barrel);
  }
  const stall = mesh(new THREE.BoxGeometry(2.4, 0.08, 1.4), mat(color.cloth));
  stall.position.set(4, 2.2, 2);
  scene.add(stall);
  for (const [dx, dz] of [[-1.1, -0.6], [1.1, -0.6], [-1.1, 0.6], [1.1, 0.6]]) {
    const post = mesh(new THREE.CylinderGeometry(0.06, 0.06, 2.2, 6), mat(color.timber));
    post.position.set(4 + dx, 1.1, 2 + dz);
    scene.add(post);
  }

  // Torch on the square.
  const torchPost = mesh(new THREE.CylinderGeometry(0.06, 0.08, 1.6, 6), mat(color.iron));
  torchPost.position.set(1.5, 0.8, 1.5);
  const flame = new THREE.PointLight(0xff9a3c, 12, 9, 2);
  flame.position.set(1.5, 1.75, 1.5);
  scene.add(torchPost, flame);

  // Scatter trees and rocks outside the village core.
  for (let i = 0; i < 70; i++) {
    const a = r() * Math.PI * 2;
    const dist = 24 + r() * 50;
    const t = tree(r);
    t.position.set(Math.cos(a) * dist, 0, Math.sin(a) * dist);
    scene.add(t);
  }
  for (let i = 0; i < 60; i++) {
    const rk = rock(r);
    const alongRiver = i < 30;
    rk.position.set(
      alongRiver ? -30 + r() * 60 : -25 + r() * 50,
      0.1,
      alongRiver ? -14 + (r() < 0.5 ? -1 : 1) * (4.2 + r() * 1.5) : -8 + r() * 36,
    );
    rk.rotation.y = r() * Math.PI;
    scene.add(rk);
  }

  // Player placeholder: capsule.
  const player = mesh(new THREE.CapsuleGeometry(0.3, 1.1, 4, 12), mat(0x3b4a6b, 0.7));
  player.position.set(1, 0.85, 4);
  scene.add(player);

  return { player };
}
