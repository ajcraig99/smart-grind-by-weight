// 3D twin: a generic stylised single-dose grinder built from primitives (boxes, cylinders, cones, spheres).
// Driven only by the twin state snapshot; contains no controller logic. Units are centimetres.
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { PANEL_W, PANEL_H } from './twin.js';

const MAX_PARTICLES = 400;
const MAX_BEANS = 450;
const BEAN_FULL_G = 30;            // bean mass that fills the hopper model
const HOPPER_BASE_Y = 15;
const HOPPER_H = 9;
const BASE_TOP = 1.5;
const LOADCELL_TOP = 2.6;
const PLAT_TOP = 3.1;
const CUP_X = 0;
const CUP_Z = 4.5;
const CUP_H = 6;
const CUP_FILL_CM_PER_G = 0.09;    // visual scale of the grounds level in the cup
const CHUTE_EXIT = new THREE.Vector3(0, 11.2, CUP_Z);
const FRAME_MS = 33;               // render at most ~30 fps
const MAX_RENDER_SHARE = 0.3;      // never spend more than this share of wall time drawing (slow GPUs)

const VIEW_NORMAL = { pos: new THREE.Vector3(23, 30, 45), target: new THREE.Vector3(0, 11, 2) };
const VIEW_EXPLODED = { pos: new THREE.Vector3(18, 34, 74), target: new THREE.Vector3(-1, 13, 5) };

function webglAvailable() {
  try {
    const c = document.createElement('canvas');
    return !!(c.getContext('webgl2') || c.getContext('webgl'));
  } catch { return false; }
}

function mulberry32(a) {
  return () => {
    a |= 0; a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const smooth = (x) => { x = Math.min(1, Math.max(0, x)); return x * x * (3 - 2 * x); };

export function createScene3D(container, screenCanvas, hooks) {
  container.textContent = '';
  if (!webglAvailable()) {
    container.innerHTML = '<div class="three-msg">3D view unavailable: this browser could not create a WebGL context. ' +
      'All other panels work normally; the 2D virtual screen is unaffected.</div>';
    return null;
  }
  let renderer;
  try {
    renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'default' });
  } catch (e) {
    container.innerHTML = '<div class="three-msg">3D view unavailable: ' + String(e && e.message || e) + '</div>';
    return null;
  }
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.5));
  const canvas = renderer.domElement;
  canvas.setAttribute('aria-label', '3D view of the grinder twin');
  canvas.className = 'three-canvas';
  container.appendChild(canvas);
  const labelLayer = document.createElement('div');
  labelLayer.className = 'three-labels';
  container.appendChild(labelLayer);

  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x0f141c);
  scene.fog = new THREE.Fog(0x0f141c, 90, 210);
  const camera = new THREE.PerspectiveCamera(38, 1, 0.5, 400);
  camera.position.copy(VIEW_NORMAL.pos);

  // ---------------------------------------------------------------- lights and floor
  scene.add(new THREE.HemisphereLight(0xdfe8ff, 0x262d3b, 1.5));
  const key = new THREE.DirectionalLight(0xffffff, 1.9);
  key.position.set(22, 42, 30);
  scene.add(key);
  const fill = new THREE.DirectionalLight(0x8fa3d8, 0.6);
  fill.position.set(-28, 16, -12);
  scene.add(fill);
  const floor = new THREE.Mesh(new THREE.CircleGeometry(70, 48), new THREE.MeshStandardMaterial({ color: 0x121821, roughness: 1 }));
  floor.rotation.x = -Math.PI / 2;
  scene.add(floor);
  const grid = new THREE.GridHelper(120, 24, 0x2a3345, 0x1b2230);
  grid.position.y = 0.02;
  scene.add(grid);

  // ---------------------------------------------------------------- materials
  const std = (color, rough = 0.6, metal = 0.1, extra = {}) => new THREE.MeshStandardMaterial({ color, roughness: rough, metalness: metal, ...extra });
  const shellMats = []; // fade out in the exploded view
  const shell = (color, rough = 0.6, metal = 0.1) => {
    const m = std(color, rough, metal, { transparent: true });
    m.userData.base = 1;
    shellMats.push(m);
    return m;
  };
  const M = {
    base: shell(0x2b313c, 0.8, 0.05),
    body: shell(0x56627a, 0.55, 0.15),
    housing: shell(0x6a7588, 0.45, 0.35),
    plate: std(0xd3d9e3, 0.3, 0.5),
    lid: std(0x596378, 0.5, 0.2),
    glass: std(0xa8c8f0, 0.1, 0, { transparent: true, opacity: 0.11, side: THREE.DoubleSide, depthWrite: false }),
    bean: std(0x6f4a2d, 0.55, 0),
    grounds: std(0x4a3022, 0.95, 0),
    metal: std(0xc4ccd8, 0.3, 0.8),
    chute: std(0x8a93a6, 0.4, 0.5),
    cup: std(0xe6e9ef, 0.3, 0, { transparent: true, opacity: 0.6, side: THREE.DoubleSide, depthWrite: false }),
    pcb: std(0x2f8f5b, 0.6, 0),
    esp: std(0x1f4f57, 0.5, 0.1),
    chip: std(0x15181e, 0.4, 0.2),
    cell: std(0xaab3c2, 0.35, 0.75),
    gauge: std(0xe08a2e, 0.5, 0),
    relay: std(0x2b6cb0, 0.5, 0.05),
    motor: std(0x5a6375, 0.45, 0.4),
    dark: std(0x10141a, 0.8, 0),
  };
  M.housing.side = THREE.DoubleSide;
  const ledCmd = new THREE.MeshStandardMaterial({ color: 0x3a1616, emissive: 0xff3b3b, emissiveIntensity: 0, roughness: 0.4 });
  const ledContact = new THREE.MeshStandardMaterial({ color: 0x3a3112, emissive: 0xffc933, emissiveIntensity: 0, roughness: 0.4 });

  const box = (w, h, d, mat, x = 0, y = 0, z = 0) => {
    const m = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), mat);
    m.position.set(x, y, z);
    return m;
  };
  const cyl = (rt, rb, h, mat, seg = 32, open = false) => new THREE.Mesh(new THREE.CylinderGeometry(rt, rb, h, seg, 1, open), mat);

  // ---------------------------------------------------------------- parts registry (exploded view)
  const parts = [];
  const addPart = (group, offset, label, anchor = [0, 0, 0]) => {
    scene.add(group);
    parts.push({ group, base: group.position.clone(), offset: new THREE.Vector3(...offset), label, anchor: new THREE.Vector3(...anchor), el: null });
    return group;
  };

  // base plate
  const base = new THREE.Group();
  base.add(box(16, BASE_TOP, 18, M.base, 0, BASE_TOP / 2, 0));
  scene.add(base);

  // column shell (motor housing)
  const column = new THREE.Group();
  column.position.set(0, BASE_TOP + 5.25, -5);
  column.add(box(9, 10.5, 8, M.body));
  column.add(box(9.4, 0.5, 8.4, M.housing, 0, 5.4, 0));
  addPart(column, [0, 0, 0], null);

  // motor (inside the column) with a spinning shaft and blades
  const motor = new THREE.Group();
  motor.position.set(0, 6.4, -5);
  motor.add(cyl(2.3, 2.3, 4.8, M.motor, 28));
  for (let i = 0; i < 6; i++) {
    const a = (i / 6) * Math.PI * 2;
    const fin = box(0.25, 4.2, 0.9, M.chute, Math.cos(a) * 2.45, 0, Math.sin(a) * 2.45);
    fin.rotation.y = -a;
    motor.add(fin);
  }
  const shaft = new THREE.Group();
  shaft.position.y = 2.4;
  shaft.add(cyl(0.45, 0.45, 2.2, M.metal, 16));
  for (let i = 0; i < 4; i++) {
    const b = box(2.4, 0.15, 0.5, M.metal, 0, 0.6, 0);
    b.rotation.y = (i * Math.PI) / 4;
    shaft.add(b);
  }
  motor.add(shaft);
  addPart(motor, [-22, -1.4, 3], 'Motor', [0, 2.8, 0]);

  // burr housing (shell) and burrs
  const housing = new THREE.Group();
  housing.position.set(0, 13.5, -5);
  housing.add(cyl(3.6, 3.6, 3, M.housing, 40, true));
  const housingTop = cyl(3.6, 3.6, 0.3, M.housing, 40);
  housingTop.position.y = 1.65;
  housing.add(housingTop);
  addPart(housing, [0, 3, 0], null);

  const burrs = new THREE.Group();
  burrs.position.set(0, 13.6, -5);
  const burrMat = M.metal;
  const lowerBurr = new THREE.Group();
  lowerBurr.add(cyl(2.9, 2.9, 0.6, burrMat, 24));
  for (let i = 0; i < 12; i++) {
    const a = (i / 12) * Math.PI * 2;
    const rib = box(0.22, 0.25, 2.3, M.dark, 0, 0.42, 0);
    rib.position.set(Math.cos(a) * 1.3, 0.42, Math.sin(a) * 1.3);
    rib.rotation.y = -a + Math.PI / 2;
    lowerBurr.add(rib);
  }
  lowerBurr.position.y = -0.35;
  burrs.add(lowerBurr);
  const upperBurr = new THREE.Group();
  upperBurr.add(cyl(2.9, 2.9, 0.6, M.chute, 24));
  upperBurr.add(cyl(1.1, 1.1, 0.5, M.dark, 16));
  upperBurr.position.y = 0.55;
  burrs.add(upperBurr);
  addPart(burrs, [0, 7.5, 0], 'Burrs', [2.6, 0.5, 0]);

  // hopper with beans
  const hopper = new THREE.Group();
  hopper.position.set(0, HOPPER_BASE_Y + HOPPER_H / 2, -5);
  hopper.add(cyl(3.45, 3.45, HOPPER_H, M.glass, 40, true));
  const lid = cyl(3.7, 3.7, 0.6, M.lid, 40);
  lid.position.y = HOPPER_H / 2 + 0.3;
  hopper.add(lid);
  const knob = cyl(0.6, 0.8, 0.5, M.metal, 16);
  knob.position.y = HOPPER_H / 2 + 0.85;
  hopper.add(knob);
  const rim = cyl(3.7, 3.7, 0.4, M.lid, 40);
  rim.position.y = -HOPPER_H / 2 - 0.2;
  hopper.add(rim);
  // beans: ellipsoids sorted bottom to top; the first n are shown
  const beanGeo = new THREE.SphereGeometry(0.5, 8, 6);
  const beans = new THREE.InstancedMesh(beanGeo, M.bean, MAX_BEANS);
  {
    const rnd = mulberry32(7);
    const items = [];
    for (let i = 0; i < MAX_BEANS; i++) {
      const r = 2.95 * Math.sqrt(rnd());
      const th = rnd() * Math.PI * 2;
      items.push({ x: Math.cos(th) * r, z: Math.sin(th) * r, u: rnd(), rot: [rnd() * 6.28, rnd() * 6.28, rnd() * 6.28], s: 0.75 + rnd() * 0.35, c: 0.75 + rnd() * 0.5 });
    }
    items.sort((a, b) => a.u - b.u);
    const dummy = new THREE.Object3D();
    const col = new THREE.Color();
    items.forEach((it, i) => {
      dummy.position.set(it.x, -HOPPER_H / 2 + 0.45 + it.u * (HOPPER_H - 0.9), it.z);
      dummy.rotation.set(...it.rot);
      dummy.scale.set(1.0 * it.s, 0.7 * it.s, 0.72 * it.s);
      dummy.updateMatrix();
      beans.setMatrixAt(i, dummy.matrix);
      col.setScalar(it.c);
      beans.setColorAt(i, col);
    });
    beans.instanceMatrix.needsUpdate = true;
    beans.instanceColor.needsUpdate = true;
    beans.count = 0;
  }
  beans.frustumCulled = false;
  hopper.add(beans);
  addPart(hopper, [0, 12, 0], 'Hopper', [3.4, 3, 0]);

  // chute
  const chuteA = new THREE.Vector3(0, 13.3, -1.4);
  const chuteB = new THREE.Vector3(0, 11.6, CUP_Z);
  const chute = new THREE.Group();
  {
    const dir = chuteB.clone().sub(chuteA);
    const len = dir.length();
    const tube = cyl(0.95, 0.75, len, M.chute, 20, true);
    tube.material = std(0x8a93a6, 0.4, 0.5, { side: THREE.DoubleSide });
    tube.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.clone().normalize());
    chute.add(tube);
    chute.position.copy(chuteA.clone().add(chuteB).multiplyScalar(0.5));
    tube.position.set(0, 0, 0);
  }
  addPart(chute, [10, 5, -1], 'Chute', [0, 0.8, 0]);

  // display module: bezel, screen, ESP32 board slab behind
  const screenW = 4.4;
  const screenH = screenW * (PANEL_H / PANEL_W);
  const display = new THREE.Group();
  display.position.set(0, 6.6, -0.95);
  display.add(box(5.3, screenH + 0.9, 0.45, M.dark, 0, 0, 0));
  const pcbSlab = box(5.0, screenH + 0.4, 0.22, M.esp, 0, 0, -0.35);
  display.add(pcbSlab);
  display.add(box(1.6, 1.2, 0.18, M.chip, 0.8, -1.4, -0.55));
  display.add(box(1.0, 0.6, 0.16, M.chip, -1.2, 1.0, -0.55));
  const screenTex = new THREE.CanvasTexture(screenCanvas);
  screenTex.colorSpace = THREE.SRGBColorSpace;
  screenTex.generateMipmaps = false;
  screenTex.minFilter = THREE.LinearFilter;
  screenTex.magFilter = THREE.LinearFilter;
  const screenMat = new THREE.MeshBasicMaterial({ map: screenTex, toneMapped: false });
  const screenMesh = new THREE.Mesh(new THREE.PlaneGeometry(screenW, screenH), screenMat);
  screenMesh.position.z = 0.24;
  display.add(screenMesh);
  addPart(display, [-3, 5.4, 15], 'Controller board (ESP32-S3 + display)', [0, screenH / 2 + 0.6, 0]);

  // relay module with two LEDs
  const relay = new THREE.Group();
  relay.position.set(6.4, 4.2, -5);
  relay.add(box(3, 2.2, 2.4, M.relay));
  relay.add(box(2.4, 0.9, 1.6, M.dark, 0, 1.5, 0));
  const l1 = new THREE.Mesh(new THREE.SphereGeometry(0.3, 16, 12), ledCmd);
  l1.position.set(-0.7, 0.45, 1.25);
  const l2 = new THREE.Mesh(new THREE.SphereGeometry(0.3, 16, 12), ledContact);
  l2.position.set(0.7, 0.45, 1.25);
  relay.add(l1, l2);
  addPart(relay, [15, 1.8, 3], 'Relay + LEDs', [0, 1.6, 0]);

  // load cell bar, HX711 board
  const cell = new THREE.Group();
  cell.position.set(0, 2.1, CUP_Z);
  cell.add(box(2.2, 1.0, 8.2, M.cell));
  for (const z of [-2.6, 2.6]) {
    const hole = cyl(0.45, 0.45, 1.05, M.dark, 16);
    hole.rotation.z = Math.PI / 2;
    hole.position.set(0, 0, z);
    cell.add(hole);
  }
  cell.add(box(1.2, 0.05, 1.4, M.gauge, 0, 0.52, -1.0));
  addPart(cell, [13, -1.1, 10], 'Load cell bar', [0, 0.8, 0]);

  const hx = new THREE.Group();
  hx.position.set(-5, 0.75, 3);
  hx.add(box(3.2, 0.25, 2.2, M.pcb));
  hx.add(box(1.0, 0.3, 0.8, M.chip, 0.4, 0.27, 0));
  hx.add(box(0.8, 0.5, 0.3, M.metal, -1.0, 0.3, -0.7));
  addPart(hx, [-14, -0.25, 13], 'HX711 board', [0, 0.8, 0]);

  // load-cell platform (deflects with the sensed mass)
  const platform = new THREE.Group();
  platform.position.set(0, PLAT_TOP - 0.25, CUP_Z);
  platform.add(box(9, 0.5, 9, M.plate));
  const foot = cyl(0.7, 0.7, 0.3, M.dark, 16);
  foot.position.y = -0.3;
  platform.add(foot);
  addPart(platform, [0, 5.5, 0], 'Weighing platform', [4.4, 0, 4.4]);

  // grounds lying on the bare platform
  const platGrounds = cyl(1, 1, 1, M.grounds, 28);
  platGrounds.visible = false;
  platform.add(platGrounds);

  // cup (follows the platform) with grounds fill
  const cup = new THREE.Group();
  scene.add(cup);
  const cupWall = new THREE.Mesh(new THREE.CylinderGeometry(3.0, 2.4, CUP_H, 40, 1, true), M.cup);
  cupWall.position.y = CUP_H / 2;
  cup.add(cupWall);
  const cupBottom = cyl(2.4, 2.4, 0.25, M.cup, 40);
  cupBottom.position.y = 0.125;
  cup.add(cupBottom);
  const fillMesh = new THREE.Mesh(new THREE.CylinderGeometry(1, 1, 1, 32), M.grounds);
  cup.add(fillMesh);
  const cupTargetPos = new THREE.Vector3(CUP_X, PLAT_TOP, CUP_Z);
  cup.position.copy(cupTargetPos);
  let cupAway = 0; // 0 seated .. 1 lifted away

  // ---------------------------------------------------------------- grounds stream
  const pPos = new Float32Array(MAX_PARTICLES * 3);
  const pVel = new Float32Array(MAX_PARTICLES);
  let pCount = 0;
  const pGeo = new THREE.BufferGeometry();
  pGeo.setAttribute('position', new THREE.BufferAttribute(pPos, 3));
  pGeo.setDrawRange(0, 0);
  const pMat = new THREE.PointsMaterial({ color: 0xdcaa72, size: 0.65, sizeAttenuation: true });
  const points = new THREE.Points(pGeo, pMat);
  points.frustumCulled = false;
  scene.add(points);
  let spawnAcc = 0;
  const PARTICLES_PER_G = 100;   // particles emitted per gram delivered at the cup
  const GRAVITY = 45;           // cm/s^2, stylised

  // ---------------------------------------------------------------- labels
  for (const p of parts) {
    if (!p.label) continue;
    const el = document.createElement('div');
    el.className = 'lbl';
    el.textContent = p.label;
    el.style.opacity = '0';
    labelLayer.appendChild(el);
    p.el = el;
  }

  // ---------------------------------------------------------------- controls and input
  const raycaster = new THREE.Raycaster();
  const ndc = new THREE.Vector2();
  const screenPlane = new THREE.Plane();
  let touchId = null;

  function setNdc(ev) {
    const r = canvas.getBoundingClientRect();
    ndc.set(((ev.clientX - r.left) / r.width) * 2 - 1, -((ev.clientY - r.top) / r.height) * 2 + 1);
    raycaster.setFromCamera(ndc, camera);
  }
  function screenHit(ev) {
    setNdc(ev);
    const hits = raycaster.intersectObject(screenMesh, false);
    if (!hits.length || !hits[0].uv) return null;
    return [hits[0].uv.x * PANEL_W, (1 - hits[0].uv.y) * PANEL_H];
  }
  // While a touch is held the finger may leave the screen: intersect the screen plane and clamp.
  function screenPlaneXY(ev) {
    setNdc(ev);
    screenMesh.updateWorldMatrix(true, false);
    const n = new THREE.Vector3(0, 0, 1).transformDirection(screenMesh.matrixWorld);
    const p0 = new THREE.Vector3().setFromMatrixPosition(screenMesh.matrixWorld);
    screenPlane.setFromNormalAndCoplanarPoint(n, p0);
    const p = new THREE.Vector3();
    if (!raycaster.ray.intersectPlane(screenPlane, p)) return null;
    screenMesh.worldToLocal(p);
    const x = (p.x / screenW + 0.5) * PANEL_W;
    const y = (0.5 - p.y / screenH) * PANEL_H;
    return [Math.min(PANEL_W - 1, Math.max(0, x)), Math.min(PANEL_H - 1, Math.max(0, y))];
  }

  // Registered before OrbitControls so a press on the 3D screen is a touch, not an orbit drag.
  canvas.addEventListener('pointerdown', (ev) => {
    if (ev.button !== 0 || touchId !== null) return;
    const hit = screenHit(ev);
    if (!hit) return;
    ev.stopImmediatePropagation();
    ev.preventDefault();
    touchId = ev.pointerId;
    canvas.setPointerCapture(ev.pointerId);
    hooks.down(hit[0], hit[1], ev.pointerId);
  });
  canvas.addEventListener('pointermove', (ev) => {
    if (touchId === ev.pointerId) {
      const xy = screenPlaneXY(ev);
      if (xy) hooks.move(xy[0], xy[1], ev.pointerId);
      return;
    }
    if (ev.buttons === 0) canvas.style.cursor = screenHit(ev) ? 'pointer' : 'grab';
  });
  const endTouch = (ev) => {
    if (touchId !== ev.pointerId) return;
    const xy = screenPlaneXY(ev);
    touchId = null;
    hooks.up(xy ? xy[0] : 0, xy ? xy[1] : 0, ev.pointerId);
  };
  canvas.addEventListener('pointerup', endTouch);
  canvas.addEventListener('pointercancel', endTouch);

  const controls = new OrbitControls(camera, canvas);
  controls.enableDamping = true;
  controls.dampingFactor = 0.12;
  controls.minDistance = 14;
  controls.maxDistance = 120;
  controls.maxPolarAngle = Math.PI * 0.49;
  controls.target.copy(VIEW_NORMAL.target);
  controls.update();
  let needsRender = true;
  controls.addEventListener('change', () => { needsRender = true; });

  // camera tween
  let tween = null;
  function flyTo(view) {
    tween = { t: 0, p0: camera.position.clone(), q0: controls.target.clone(), view };
  }

  // ---------------------------------------------------------------- sizing and visibility
  let active = true;
  function resize() {
    const w = Math.max(50, container.clientWidth);
    const h = Math.max(50, container.clientHeight);
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
    needsRender = true;
  }
  new ResizeObserver(resize).observe(container);
  resize();
  canvas.addEventListener('webglcontextlost', (e) => { e.preventDefault(); active = false; });

  // ---------------------------------------------------------------- per-frame update
  let explodeTarget = 0;
  let explode = 0;
  let lastDraw = 0;
  let renderCostMs = 0;
  let lastKey = '';
  const tmp = new THREE.Vector3();
  const chuteExitWorld = new THREE.Vector3();
  let platDy = 0;
  let lastWall = 0;
  let burrAngle = 0;
  const rnd = mulberry32(99);

  function applyExplode(dt) {
    const k = 1 - Math.exp(-dt * 5);
    const prev = explode;
    explode += (explodeTarget - explode) * k;
    if (Math.abs(explodeTarget - explode) < 0.002) explode = explodeTarget;
    if (explode !== prev) {
      const e = smooth(explode);
      for (const p of parts) p.group.position.copy(p.base).addScaledVector(p.offset, e);
      for (const m of shellMats) {
        const o = m.userData.base + (0.1 - m.userData.base) * e;
        m.opacity = o;
        m.depthWrite = o > 0.6;
      }
      needsRender = true;
    }
  }

  function updateTween(dt) {
    if (!tween) return;
    tween.t = Math.min(1, tween.t + dt / 0.8);
    const e = smooth(tween.t);
    camera.position.lerpVectors(tween.p0, tween.view.pos, e);
    controls.target.lerpVectors(tween.q0, tween.view.target, e);
    if (tween.t >= 1) tween = null;
    needsRender = true;
  }

  // state: twin snapshot; wallMs: performance.now(); speed: virtual speed; paused
  function frame(wallMs, state, speed, paused) {
    if (!active) return;
    const dt = lastWall ? Math.min(0.1, (wallMs - lastWall) / 1000) : 0.016;
    lastWall = wallMs;
    applyExplode(dt);
    updateTween(dt);
    controls.update();

    if (state) {
      const animDt = paused ? 0 : dt * Math.min(2, Math.max(0.25, speed));

      // screen brightness and power
      const b = state.display_on ? Math.max(0.06, Math.min(1, state.brightness)) : 0;
      screenMat.color.setScalar(b);

      // beans
      const beanG = (state.m_hopper_g || 0) + (state.m_burr_g || 0);
      const n = Math.max(0, Math.min(MAX_BEANS, Math.round((beanG / BEAN_FULL_G) * MAX_BEANS)));
      if (beans.count !== n) { beans.count = n; needsRender = true; }

      // burrs and motor
      const ms = state.motor_speed || 0;
      burrAngle += ms * 16 * animDt;
      lowerBurr.rotation.y = burrAngle;
      shaft.rotation.y = burrAngle * 1.3;
      if (ms > 0.01 && animDt > 0) needsRender = true;

      // relay LEDs
      ledCmd.emissiveIntensity = state.relay_pin ? 2.2 : 0;
      ledContact.emissiveIntensity = state.relay_contact ? 2.2 : 0;

      // platform deflection (exaggerated) from the sensed mass
      const dyT = -0.6 * Math.tanh(Math.max(-200, Math.min(2000, state.scale_signal_g || 0)) / 300);
      platDy += (dyT - platDy) * (1 - Math.exp(-dt * 25));
      if (Math.abs(dyT - platDy) < 1e-4) platDy = dyT;
      const plat = parts.find((p) => p.group === platform);
      platform.position.y = plat.base.y + plat.offset.y * smooth(explode) + platDy;
      cell.rotation.x = platDy * 0.03;

      // cup seat / lift-away
      const awayT = state.cup_present ? 0 : 1;
      cupAway += (awayT - cupAway) * (1 - Math.exp(-dt * 7));
      if (Math.abs(awayT - cupAway) < 0.002) cupAway = awayT;
      const platTopY = platform.position.y + 0.25;
      cup.position.set(CUP_X + cupAway * 10, platTopY + cupAway * 7, CUP_Z + cupAway * 5);
      const opq = 0.6 * (1 - cupAway);
      M.cup.opacity = opq;
      cup.visible = cupAway < 0.98;
      const fillH = Math.min(CUP_H - 0.5, Math.max(0, (state.m_cup_g || 0) * CUP_FILL_CM_PER_G));
      fillMesh.visible = fillH > 0.02;
      if (fillMesh.visible) {
        const r = 2.4 + (fillH / CUP_H) * 0.6 - 0.12;
        fillMesh.scale.set(r, fillH, r);
        fillMesh.position.y = 0.25 + fillH / 2;
      }
      grounds_visible(state);

      // particles
      const flow = state.flow_cup_gps > 0 ? state.flow_cup_gps : 0;
      const exitP = parts.find((p) => p.group === chute);
      // the exit point follows the chute group: base exit + current offset
      chuteExitWorld.copy(CHUTE_EXIT).add(chute.position).sub(exitP.base);
      if (animDt > 0) {
        spawnAcc += flow * PARTICLES_PER_G * animDt;
        while (spawnAcc >= 1 && pCount < MAX_PARTICLES) {
          spawnAcc -= 1;
          const i = pCount++;
          // spread the emission over the frame so a slow frame rate still shows a continuous stream
          const age = rnd() * animDt;
          pVel[i] = GRAVITY * age;
          pPos[i * 3] = chuteExitWorld.x + (rnd() - 0.5) * 0.5;
          pPos[i * 3 + 1] = chuteExitWorld.y - rnd() * 0.3 - 0.5 * GRAVITY * age * age;
          pPos[i * 3 + 2] = chuteExitWorld.z + (rnd() - 0.5) * 0.5;
        }
        if (spawnAcc > 60 || flow <= 0) spawnAcc = 0;
        const overCup = (x, z) => Math.abs(x - cup.position.x) < 2.6 && Math.abs(z - cup.position.z) < 2.6 && cup.visible;
        for (let i = 0; i < pCount;) {
          pVel[i] += GRAVITY * animDt;
          pPos[i * 3 + 1] -= pVel[i] * animDt;
          const x = pPos[i * 3], z = pPos[i * 3 + 2];
          const floorY = overCup(x, z) ? cup.position.y + 0.25 + fillH : platform.position.y + 0.25;
          if (pPos[i * 3 + 1] <= floorY) {
            const j = --pCount;
            pPos[i * 3] = pPos[j * 3]; pPos[i * 3 + 1] = pPos[j * 3 + 1]; pPos[i * 3 + 2] = pPos[j * 3 + 2];
            pVel[i] = pVel[j];
          } else i++;
        }
        pGeo.attributes.position.needsUpdate = true;
        pGeo.setDrawRange(0, pCount);
      }
      if (pCount > 0) needsRender = true;

      const k = `${ms.toFixed(2)}|${state.cup_present}|${(state.m_cup_g || 0).toFixed(2)}|${state.relay_pin}|${state.relay_contact}|${(state.scale_signal_g || 0).toFixed(0)}|${state.brightness}|${state.display_on}|${(state.m_platform_g || 0).toFixed(2)}|${n}`;
      if (k !== lastKey) { lastKey = k; needsRender = true; }
    }
    if (cupAway > 0 && cupAway < 1) needsRender = true;

    if (needsRender && wallMs - lastDraw >= Math.max(FRAME_MS, renderCostMs / MAX_RENDER_SHARE)) {
      lastDraw = wallMs;
      needsRender = false;
      const t0 = performance.now();
      updateLabels();
      renderer.render(scene, camera);
      const cost = performance.now() - t0;
      renderCostMs += (cost - renderCostMs) * 0.3; // smoothed draw cost; slows the 3D frame rate on weak GPUs
      stats.frames += 1;
    }
  }

  function grounds_visible(state) {
    const m = state.m_platform_g || 0;
    platGrounds.visible = m > 0.005;
    if (platGrounds.visible) {
      const r = Math.min(4.2, 0.35 + 0.5 * Math.sqrt(m));
      const h = 0.12 + Math.min(0.5, m * 0.02);
      platGrounds.scale.set(r, h, r);
      platGrounds.position.set(0, 0.25 + h / 2, 0);
    }
  }

  const stats = { frames: 0 };

  function updateLabels() {
    const e = smooth(explode);
    const w = canvas.clientWidth, h = canvas.clientHeight;
    for (const p of parts) {
      if (!p.el) continue;
      const vis = e > 0.55 ? (e - 0.55) / 0.45 : 0;
      p.el.style.opacity = String(vis);
      if (vis <= 0) continue;
      p.group.updateWorldMatrix(true, false);
      tmp.copy(p.anchor).applyMatrix4(p.group.matrixWorld).project(camera);
      const behind = tmp.z > 1;
      p.el.style.opacity = behind ? '0' : String(vis);
      p.el.style.transform = `translate(${((tmp.x + 1) / 2) * w}px, ${((1 - tmp.y) / 2) * h}px) translate(-50%, -130%)`;
    }
  }

  return {
    canvas,
    frame,
    get exploded() { return explodeTarget === 1; },
    setExploded(on) {
      explodeTarget = on ? 1 : 0;
      flyTo(on ? VIEW_EXPLODED : VIEW_NORMAL);
      needsRender = true;
    },
    resetView() { flyTo(explodeTarget ? VIEW_EXPLODED : VIEW_NORMAL); },
    markScreenDirty() { screenTex.needsUpdate = true; needsRender = true; },
    wake() { needsRender = true; },
    get explodeAmount() { return explode; },
    get stats() { return stats; },
    get renderCostMs() { return renderCostMs; },
    get particles() { return pCount; },
    get labelsVisible() { return parts.filter((p) => p.el && parseFloat(p.el.style.opacity) > 0.5).length; },
    // Test aid: render now and count pixels that differ from the background (readPixels right after the draw).
    probe() {
      updateLabels();
      renderer.render(scene, camera);
      const gl = renderer.getContext();
      const w = gl.drawingBufferWidth, h = gl.drawingBufferHeight;
      const buf = new Uint8Array(w * h * 4);
      gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, buf);
      let lit = 0;
      for (let i = 0; i < buf.length; i += 4) {
        if (Math.abs(buf[i] - 15) > 14 || Math.abs(buf[i + 1] - 20) > 14 || Math.abs(buf[i + 2] - 28) > 14) lit++;
      }
      return { w, h, lit, total: w * h };
    },
    // Client (page) coordinates of a panel pixel shown on the 3D screen.
    panelToClient(px, py) {
      camera.updateMatrixWorld();
      screenMesh.updateWorldMatrix(true, false);
      const v = new THREE.Vector3((px / PANEL_W - 0.5) * screenW, (0.5 - py / PANEL_H) * screenH, 0);
      screenMesh.localToWorld(v);
      v.project(camera);
      const r = canvas.getBoundingClientRect();
      return [r.left + ((v.x + 1) / 2) * r.width, r.top + ((1 - v.y) / 2) * r.height];
    },
    get ready() { return active; },
  };
}
