import * as THREE from './vendor/three.module.js';
import { OrbitControls } from './vendor/OrbitControls.js';
import { ViewHelper } from './vendor/ViewHelper.js';

const overlay = document.getElementById('overlay');
const host = document.getElementById('app');

const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
renderer.setPixelRatio(window.devicePixelRatio || 1);
renderer.setClearColor(0x161616, 1);
renderer.autoClear = false;
host.appendChild(renderer.domElement);

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(42, 1, 0.1, 10000);
camera.up.set(0, 0, 1);

const controls = createControls(camera);

const ambient = new THREE.AmbientLight(0xffffff, 0.7);
scene.add(ambient);

const keyLight = new THREE.DirectionalLight(0xffffff, 0.55);
keyLight.position.set(-1.2, -1.4, 2.0);
scene.add(keyLight);

const world = new THREE.Group();
scene.add(world);

const machineMarker = new THREE.Group();
scene.add(machineMarker);

let currentScene = null;
let currentState = null;
let currentSceneVersion = -1;
let currentResetToken = -1;
let currentCameraPreset = 'Iso';

const materials = {
  rapidCompleted: new THREE.LineBasicMaterial({ transparent: true, opacity: 0.52, vertexColors: true }),
  rapidRemaining: new THREE.LineBasicMaterial({ transparent: true, opacity: 0.22, vertexColors: true }),
  plungeCompleted: new THREE.LineBasicMaterial({ transparent: true, opacity: 0.7, vertexColors: true }),
  plungeRemaining: new THREE.LineBasicMaterial({ transparent: true, opacity: 0.28, vertexColors: true }),
  arcCompleted: new THREE.LineBasicMaterial({ transparent: true, opacity: 0.95, vertexColors: true }),
  arcRemaining: new THREE.LineBasicMaterial({ transparent: true, opacity: 0.55, vertexColors: true }),
  cutCompleted: new THREE.LineBasicMaterial({ transparent: true, opacity: 0.95, vertexColors: true }),
  cutRemaining: new THREE.LineBasicMaterial({ transparent: true, opacity: 0.6, vertexColors: true })
};

const clock = new THREE.Clock();
const viewHelper = new ViewHelper(camera, renderer.domElement);
viewHelper.setLabels('X', 'Y', 'Z');

renderer.domElement.addEventListener('click', e => {
  if (!hasLoadedToolpath()) {
    return;
  }

  viewHelper.handleClick(e);
});

renderer.domElement.addEventListener('mousemove', e => {
  if (!hasLoadedToolpath()) {
    renderer.domElement.style.cursor = '';
    return;
  }

  const rect = renderer.domElement.getBoundingClientRect();
  const dim = 128;
  const x = e.clientX - rect.left;
  const y = e.clientY - rect.top;
  const w = renderer.domElement.clientWidth;
  const h = renderer.domElement.clientHeight;
  renderer.domElement.style.cursor = (x > w - dim && y < dim) ? 'pointer' : '';
});

const resizeObserver = new ResizeObserver(() => resize());
resizeObserver.observe(host);
resize();
animate();
poll();
setInterval(poll, 1000);

window.setToolpathScene = (scene) => {
  if (!scene) {
    return;
  }

  currentScene = scene;
  currentSceneVersion = scene.sceneVersion ?? currentSceneVersion;
  buildScene();
  if (currentState) {
    applyState();
  }
};

window.setToolpathState = (state) => {
  if (!state) {
    return;
  }

  currentState = state;
  applyState();
};

async function poll() {
  try {
    const state = await fetchJson('./state');
    if (!state) {
      return;
    }

    currentState = state;
    if (state.sceneVersion !== currentSceneVersion) {
      currentScene = await fetchJson('./scene');
      currentSceneVersion = state.sceneVersion;
      buildScene();
    }

    applyState();
  } catch (error) {
    overlay.textContent = 'Unable to load the 3D viewer state.';
    overlay.style.display = 'flex';
  }
}

function buildScene() {
  world.clear();

  if (!currentScene || !currentScene.hasScene || !currentScene.bounds) {
    overlay.textContent = 'Select a G-code file to build the 3D preview.';
    overlay.style.display = 'flex';
    currentSceneVersion = currentScene?.sceneVersion ?? -1;
    return;
  }

  overlay.style.display = 'none';

  const { bounds } = currentScene;
  const min = new THREE.Vector3(bounds.minX, bounds.minY, bounds.minZ);
  const max = new THREE.Vector3(bounds.maxX, bounds.maxY, currentScene.stockTop);
  const size = new THREE.Vector3().subVectors(max, min);
  const center = new THREE.Vector3().addVectors(min, max).multiplyScalar(0.5);

  const span = Math.max(size.x, size.y, 10);
  const gridSize = Math.max(span * 1.3, 30);
  const gridDivisions = Math.max(10, Math.round(gridSize / chooseGridStep(span)));
  const grid = new THREE.GridHelper(gridSize, gridDivisions, 0x25313d, 0x1e2934);
  grid.rotation.x = Math.PI / 2;
  grid.position.set(center.x, center.y, bounds.minZ);
  world.add(grid);

  const axes = new THREE.AxesHelper(Math.max(Math.max(size.x, size.y), Math.max(size.z, 10)) * 0.16);
  axes.position.set(bounds.minX, bounds.minY, bounds.minZ);
  world.add(axes);

  const stockGeometry = new THREE.BoxGeometry(
    Math.max(size.x, 0.01),
    Math.max(size.y, 0.01),
    Math.max(max.z - bounds.minZ, 0.01)
  );
  stockGeometry.translate(center.x, center.y, bounds.minZ + ((max.z - bounds.minZ) * 0.5));
  const stockEdges = new THREE.EdgesGeometry(stockGeometry);
  const stockLines = new THREE.LineSegments(
    stockEdges,
    new THREE.LineBasicMaterial({ color: 0x3a5367, transparent: true, opacity: currentState?.showStockBox ? 0.22 : 0 })
  );
  stockLines.name = 'stock-box';
  world.add(stockLines);

  rebuildToolpaths();
  fitCamera(currentCameraPreset, false);
}

function rebuildToolpaths() {
  for (const child of [...world.children]) {
    if (child.name === 'toolpath') {
      world.remove(child);
      child.geometry?.dispose?.();
    }
  }

  if (!currentScene || !currentScene.hasScene || !currentScene.bounds || !currentState) {
    return;
  }

  const bounds = currentScene.bounds;
  const depthRange = Math.max(bounds.maxZ - bounds.minZ, 0.0001);
  const buckets = {
    rapidCompleted: createBucket(),
    rapidRemaining: createBucket(),
    plungeCompleted: createBucket(),
    plungeRemaining: createBucket(),
    arcCompleted: createBucket(),
    arcRemaining: createBucket(),
    cutCompleted: createBucket(),
    cutRemaining: createBucket()
  };

  for (const segment of currentScene.segments) {
    const completed = currentState.currentLine > 0 && segment.sourceLine <= currentState.currentLine;
    const visible = segment.isRapid
      ? currentState.showRapids
      : segment.isPlungeOrRetract
        ? currentState.showPlunges
        : segment.isArc
          ? currentState.showArcs
          : currentState.showCuts;

    const progressVisible = completed ? currentState.showCompletedPath : currentState.showRemainingPath;
    if (!visible || !progressVisible) {
      continue;
    }

    const key = segment.isRapid
      ? (completed ? 'rapidCompleted' : 'rapidRemaining')
      : segment.isPlungeOrRetract
        ? (completed ? 'plungeCompleted' : 'plungeRemaining')
        : segment.isArc
          ? (completed ? 'arcCompleted' : 'arcRemaining')
          : (completed ? 'cutCompleted' : 'cutRemaining');

    const bucket = buckets[key];
    const start = segment.start;
    const end = segment.end;
    bucket.positions.push(start[0], start[1], start[2], end[0], end[1], end[2]);

    const color = getSegmentColor(segment, completed, bounds.minZ, depthRange);
    bucket.colors.push(color.r, color.g, color.b, color.r, color.g, color.b);
  }

  for (const [key, bucket] of Object.entries(buckets)) {
    if (bucket.positions.length === 0) {
      continue;
    }

    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.Float32BufferAttribute(bucket.positions, 3));
    geometry.setAttribute('color', new THREE.Float32BufferAttribute(bucket.colors, 3));

    const lines = new THREE.LineSegments(geometry, materials[key]);
    lines.name = 'toolpath';
    world.add(lines);
  }
}

function applyState() {
  if (!currentState) {
    return;
  }

  const stockBox = world.getObjectByName('stock-box');
  if (stockBox) {
    stockBox.visible = currentState.showStockBox;
  }

  rebuildToolpaths();
  updateMachineMarker(currentState.positionX, currentState.positionY, currentState.positionZ);

  if (currentState.resetViewToken !== currentResetToken || currentState.cameraPreset !== currentCameraPreset) {
    currentResetToken = currentState.resetViewToken;
    currentCameraPreset = currentState.cameraPreset;
    fitCamera(currentCameraPreset, false);
  }
}

function updateMachineMarker(x, y, z) {
  machineMarker.clear();

  const color = new THREE.Color(0xffffff);
  const points = [
    new THREE.Vector3(x - 1.8, y, z),
    new THREE.Vector3(x + 1.8, y, z),
    new THREE.Vector3(x, y - 1.8, z),
    new THREE.Vector3(x, y + 1.8, z),
    new THREE.Vector3(x, y, z - 1.8),
    new THREE.Vector3(x, y, z + 1.8)
  ];

  const positions = [];
  const colors = [];
  for (let i = 0; i < points.length; i += 2) {
    positions.push(points[i].x, points[i].y, points[i].z, points[i + 1].x, points[i + 1].y, points[i + 1].z);
    colors.push(color.r, color.g, color.b, color.r, color.g, color.b);
  }

  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geometry.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));

  const marker = new THREE.LineSegments(geometry, new THREE.LineBasicMaterial({ vertexColors: true }));
  machineMarker.add(marker);
}

function fitCamera(preset, preserveDistance) {
  if (!currentScene || !currentScene.hasScene || !currentScene.bounds) {
    return;
  }

  const bounds = currentScene.bounds;
  const min = new THREE.Vector3(bounds.minX, bounds.minY, bounds.minZ);
  const max = new THREE.Vector3(bounds.maxX, bounds.maxY, currentScene.stockTop);
  const box = new THREE.Box3(min, max);
  const center = box.getCenter(new THREE.Vector3());
  const size = box.getSize(new THREE.Vector3());

  const fittedDistance = computeFitDistance(size);
  const distance = preserveDistance && camera.position.length() > 0
    ? camera.position.distanceTo(controls.target)
    : fittedDistance;

  const direction = getPresetDirection(preset);
  controls.target.copy(center);
  camera.position.copy(center).addScaledVector(direction, distance);
  camera.near = Math.max(distance / 200, 0.1);
  camera.far = Math.max(distance * 40, 500);
  camera.updateProjectionMatrix();
  controls.update();
}

function computeFitDistance(size) {
  const verticalFov = THREE.MathUtils.degToRad(camera.fov);
  const aspect = Math.max(camera.aspect, 0.1);
  const horizontalFov = 2 * Math.atan(Math.tan(verticalFov / 2) * aspect);

  const widthDistance = (Math.max(size.x, 10) * 0.5) / Math.tan(horizontalFov / 2);
  const heightDistance = (Math.max(size.y, size.z, 10) * 0.5) / Math.tan(verticalFov / 2);
  const radiusDistance = new THREE.Vector3(
    Math.max(size.x, 10),
    Math.max(size.y, 10),
    Math.max(size.z, 10)
  ).length() * 0.5;

  return Math.max(widthDistance, heightDistance, radiusDistance) * 1.2;
}

function getPresetDirection(preset) {
  switch ((preset || 'iso').toLowerCase()) {
    case 'top':    return new THREE.Vector3(0, 0, 1).normalize();
    case 'bottom': return new THREE.Vector3(0, 0, -1).normalize();
    case 'front':  return new THREE.Vector3(0, -1, 0.4).normalize();
    case 'back':   return new THREE.Vector3(0, 1, 0.4).normalize();
    case 'right':  return new THREE.Vector3(1, 0, 0.4).normalize();
    case 'left':   return new THREE.Vector3(-1, 0, 0.4).normalize();
    default:       return new THREE.Vector3(-1.2, -1.15, 0.82).normalize();
  }
}

function getSegmentColor(segment, completed, minZ, depthRange) {
  const depth = clamp((segment.averageZ - minZ) / depthRange, 0, 1);

  if (segment.isRapid) {
    return completed ? new THREE.Color(0xf0b850) : new THREE.Color(0x8a6930);
  }

  if (segment.isPlungeOrRetract) {
    return mixColor(completed ? 0xe08dff : 0x744689, completed ? 0xffc6ff : 0x9b6daf, depth);
  }

  if (segment.isArc) {
    return mixColor(completed ? 0x58f0dd : 0x2a8e84, completed ? 0xa9e6ff : 0x38647b, depth);
  }

  return mixColor(completed ? 0x67a6ff : 0x336198, completed ? 0xb6eeff : 0x456b83, depth);
}

function mixColor(startHex, endHex, t) {
  const start = new THREE.Color(startHex);
  const end = new THREE.Color(endHex);
  return start.lerp(end, t);
}

function chooseGridStep(span) {
  if (span <= 20) return 2;
  if (span <= 50) return 5;
  if (span <= 100) return 10;
  if (span <= 250) return 20;
  return 50;
}

function createBucket() {
  return { positions: [], colors: [] };
}

function hasLoadedToolpath() {
  return Boolean(currentScene && currentScene.hasScene && currentScene.bounds);
}

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function resize() {
  const width = Math.max(host.clientWidth, 1);
  const height = Math.max(host.clientHeight, 1);
  renderer.setSize(width, height, false);
  camera.aspect = width / height;
  camera.updateProjectionMatrix();
}

function animate() {
  requestAnimationFrame(animate);
  const delta = clock.getDelta();
  if (hasLoadedToolpath()) {
    if (viewHelper.animating) viewHelper.update(delta);
    viewHelper.center.copy(controls.target);
  }

  controls.update();
  renderer.clear();
  renderer.render(scene, camera);

  if (hasLoadedToolpath()) {
    viewHelper.render(renderer);
  }
}

function createControls(camera) {
  const nextControls = new OrbitControls(camera, renderer.domElement);
  nextControls.enableDamping = true;
  nextControls.dampingFactor = 0.08;
  nextControls.target.set(0, 0, 0);
  nextControls.screenSpacePanning = true;

  return nextControls;
}

async function fetchJson(url) {
  const response = await fetch(`${url}?_=${Date.now()}`, { cache: 'no-store' });
  if (!response.ok) {
    return null;
  }

  return await response.json();
}
