import * as THREE from './vendor/three.module.js';
import { OrbitControls } from './vendor/OrbitControls.js';
import { ViewHelper } from './vendor/ViewHelper.js';

const overlay = document.getElementById('overlay');
const host = document.getElementById('app');
const themeQuery = window.matchMedia('(prefers-color-scheme: dark)');

const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
renderer.setPixelRatio(window.devicePixelRatio || 1);
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
let toolMarkerRoot = null;
let toolMarkerGlow = null;
let toolMarkerTarget = new THREE.Vector3();
let toolMarkerDisplay = new THREE.Vector3();
let toolMarkerInitialized = false;
let toolMarkerUseSmoothing = true;
let playbackKeyframes = [];
const jsPlay = { mode: 'stopped', direction: 1, index: 0, sub: 0, speed: 12, frozen: false };
let toolpathRenderSignature = '';

const materials = {
  rapidCompleted: new THREE.LineBasicMaterial({ transparent: true, opacity: 0.42, vertexColors: true }),
  rapidRemaining: new THREE.LineBasicMaterial({ transparent: true, opacity: 0.42, vertexColors: true }),
  plungeCompleted: new THREE.LineBasicMaterial({ transparent: true, opacity: 0.82, vertexColors: true }),
  plungeRemaining: new THREE.LineBasicMaterial({ transparent: true, opacity: 0.82, vertexColors: true }),
  arcCompleted: new THREE.LineBasicMaterial({ transparent: true, opacity: 0.88, vertexColors: true }),
  arcRemaining: new THREE.LineBasicMaterial({ transparent: true, opacity: 0.88, vertexColors: true }),
  cutCompleted: new THREE.LineBasicMaterial({ transparent: true, opacity: 0.88, vertexColors: true }),
  cutRemaining: new THREE.LineBasicMaterial({ transparent: true, opacity: 0.88, vertexColors: true })
};
const sharedToolpathMaterials = new Set(Object.values(materials));

const clock = new THREE.Clock();
const viewHelper = new ViewHelper(camera, renderer.domElement);
viewHelper.setLabels('X', 'Y', 'Z');

if (typeof themeQuery.addEventListener === 'function') {
  themeQuery.addEventListener('change', handleThemeChanged);
} else if (typeof themeQuery.addListener === 'function') {
  themeQuery.addListener(handleThemeChanged);
}

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
applyViewerTheme();
resize();
animate();
poll();
setInterval(poll, 200);

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
    // Report current JS playback position back to C# so it can sync the scrubber.
    // jsPlay uses an extra initial-position keyframe at index 0, so subtract 1
    // to align with C#'s _playbackLineSequence indices.
    const isActive = jsPlay.mode !== 'stopped' || jsPlay.frozen;
    const reportKf = jsPlay.index - 1;
    const stateUrl = (isActive && reportKf >= 0)
      ? `./state?kf=${reportKf}&done=${jsPlay.frozen ? 1 : 0}`
      : './state';
    const state = await fetchJson(stateUrl);
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
  } catch (_) {
    // Transient errors are silently ignored; the next poll will retry.
  }
}

function buildScene() {
  disposeWorldContents();
  rebuildToolMarker();
  toolpathRenderSignature = '';

  if (!currentScene || !currentScene.hasScene || !currentScene.bounds) {
    overlay.textContent = 'Select a G-code file to build the 3D preview.';
    overlay.style.display = 'flex';
    currentSceneVersion = currentScene?.sceneVersion ?? -1;
    playbackKeyframes = [];
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
  const colors = getThemeColors();
  const grid = new THREE.GridHelper(gridSize, gridDivisions, colors.gridPrimary, colors.gridSecondary);
  grid.rotation.x = Math.PI / 2;
  grid.position.set(center.x, center.y, bounds.minZ);
  grid.name = 'grid';
  grid.visible = currentState?.showGrid ?? true;
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
    new THREE.LineBasicMaterial({
      color: colors.stockBox,
      transparent: true,
      opacity: currentState?.showStockBox ? 0.22 : 0
    })
  );
  stockLines.name = 'stock-box';
  world.add(stockLines);

  rebuildToolpaths();
  playbackKeyframes = buildPlaybackKeyframes(currentScene.segments ?? []);
  jsPlay.mode = 'stopped';
  jsPlay.index = 0;
  jsPlay.sub = 0;
  jsPlay.frozen = false;
  fitCamera(currentCameraPreset, false);
}

function rebuildToolpaths() {
  for (const child of [...world.children]) {
    if (child.name === 'toolpath' || child.name === 'toolpath-points') {
      world.remove(child);
      disposeRenderableNode(child);
    }
  }

  if (!currentScene || !currentScene.hasScene || !currentScene.bounds || !currentState) {
    return;
  }

  const bounds = currentScene.bounds;
  const depthRange = Math.max(bounds.maxZ - bounds.minZ, 0.0001);
  const currentLine = getToolpathProgressLine(currentState);
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
  const pointCloud = createBucket();
  let lastPointKey = null;
  let lastVisibleSourceLine = null;
  let pendingLineEnd = null;
  let pendingLineColor = null;

  for (const segment of currentScene.segments) {
    const visible = segment.isRapid
      ? currentState.showRapids
      : segment.isPlungeOrRetract
        ? currentState.showPlunges
        : segment.isArc
          ? currentState.showArcs
          : currentState.showCuts;

    const showAnyPath = currentState.showCompletedPath || currentState.showRemainingPath;
    if (!visible || !showAnyPath) {
      continue;
    }

    const pathPhase = segment.sourceLine <= currentLine ? 'Completed' : 'Remaining';
    if ((pathPhase === 'Completed' && !currentState.showCompletedPath)
      || (pathPhase === 'Remaining' && !currentState.showRemainingPath)) {
      continue;
    }

    const key = segment.isRapid
      ? `rapid${pathPhase}`
      : segment.isPlungeOrRetract
        ? `plunge${pathPhase}`
        : segment.isArc
          ? `arc${pathPhase}`
          : `cut${pathPhase}`;

    const bucket = buckets[key];
    const start = segment.start;
    const end = segment.end;
    bucket.positions.push(start[0], start[1], start[2], end[0], end[1], end[2]);

    const color = getSegmentColor(segment, false, bounds.minZ, depthRange);
    bucket.colors.push(color.r, color.g, color.b, color.r, color.g, color.b);

    if (lastVisibleSourceLine === null) {
      appendPointIfDistinct(pointCloud, start, color, lastPointKey);
      lastPointKey = getPointKey(start);
    } else if (segment.sourceLine !== lastVisibleSourceLine && pendingLineEnd && pendingLineColor) {
      appendPointIfDistinct(pointCloud, pendingLineEnd, pendingLineColor, lastPointKey);
      lastPointKey = getPointKey(pendingLineEnd);
    }

    lastVisibleSourceLine = segment.sourceLine;
    pendingLineEnd = end;
    pendingLineColor = color;
  }

  if (pendingLineEnd && pendingLineColor) {
    appendPointIfDistinct(pointCloud, pendingLineEnd, pendingLineColor, lastPointKey);
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

  if (currentState.showToolpathPoints && pointCloud.positions.length > 0) {
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.Float32BufferAttribute(pointCloud.positions, 3));
    geometry.setAttribute('color', new THREE.Float32BufferAttribute(pointCloud.colors, 3));

    const points = new THREE.Points(
      geometry,
      new THREE.PointsMaterial({
        size: 1.8,
        sizeAttenuation: true,
        vertexColors: true,
        transparent: true,
        opacity: 0.9
      })
    );
    points.name = 'toolpath-points';
    world.add(points);
  }
}

function appendPointIfDistinct(bucket, point, color, lastPointKey) {
  const pointKey = getPointKey(point);
  if (pointKey === lastPointKey) {
    return;
  }

  bucket.positions.push(point[0], point[1], point[2]);
  bucket.colors.push(color.r, color.g, color.b);
}

function getPointKey(point) {
  return `${point[0].toFixed(4)}|${point[1].toFixed(4)}|${point[2].toFixed(4)}`;
}

function getToolpathProgressLine(state) {
  const value = Number(state?.currentLine ?? 0);
  if (!Number.isFinite(value)) {
    return 0;
  }

  return Math.max(0, value);
}

function applyState() {
  if (!currentState) {
    return;
  }

  const grid = world.getObjectByName('grid');
  if (grid) {
    grid.visible = currentState.showGrid;
  }

  const stockBox = world.getObjectByName('stock-box');
  if (stockBox) {
    stockBox.visible = currentState.showStockBox;
  }

  const nextToolpathSignature = getToolpathRenderSignature(currentState);
  if (nextToolpathSignature !== toolpathRenderSignature) {
    toolpathRenderSignature = nextToolpathSignature;
    rebuildToolpaths();
  }

  updateMachineMarker(currentState);

  if (currentState.resetViewToken !== currentResetToken || currentState.cameraPreset !== currentCameraPreset) {
    currentResetToken = currentState.resetViewToken;
    currentCameraPreset = currentState.cameraPreset;
    fitCamera(currentCameraPreset, false);
  }
}

function getToolpathRenderSignature(state) {
  return [
    state.showRapids,
    state.showCuts,
    state.showArcs,
    state.showPlunges,
    state.showCompletedPath,
    state.showRemainingPath,
    state.showToolpathPoints,
    getToolpathProgressLine(state)
  ].join('|');
}

function rebuildToolMarker() {
  disposeMarkerContents();

  if (!currentScene || !currentScene.hasScene || !currentScene.bounds) {
    toolMarkerRoot = null;
    toolMarkerGlow = null;
    toolMarkerInitialized = false;
    return;
  }

  const bounds = currentScene.bounds;
  const span = Math.max(
    bounds.maxX - bounds.minX,
    bounds.maxY - bounds.minY,
    currentScene.stockTop - bounds.minZ,
    20
  );
  const radius = clamp(span * 0.012, 0.8, 5.5);
  const heightScale = 1.65;
  const tipLength = radius * 1.6 * heightScale;
  const fluteLength = radius * 3.0 * heightScale;
  const shankLength = radius * 4.6 * heightScale;
  const collarLength = radius * 1.35 * heightScale;

  toolMarkerRoot = new THREE.Group();
  toolMarkerRoot.visible = false;

  const shankMaterial = new THREE.MeshStandardMaterial({
    color: 0xd9e2ec,
    metalness: 0.85,
    roughness: 0.24
  });
  const cutterMaterial = new THREE.MeshStandardMaterial({
    color: 0x72808d,
    metalness: 0.7,
    roughness: 0.38
  });
  const accentMaterial = new THREE.MeshStandardMaterial({
    color: 0x5fd7ff,
    emissive: 0x123642,
    metalness: 0.35,
    roughness: 0.28
  });
  const glowMaterial = new THREE.MeshBasicMaterial({
    color: 0x8fe8ff,
    transparent: true,
    opacity: 0.65,
    depthWrite: false
  });

  const tipGeometry = new THREE.ConeGeometry(radius * 0.34, tipLength, 20);
  // Anchor the marker at the cutter tip so the mesh sits on the toolpath position.
  tipGeometry.rotateX(Math.PI / 2);
  const tip = new THREE.Mesh(tipGeometry, cutterMaterial);
  tip.position.z = tipLength * 0.5;
  toolMarkerRoot.add(tip);

  const fluteGeometry = new THREE.CylinderGeometry(radius * 0.22, radius * 0.28, fluteLength, 18);
  fluteGeometry.rotateX(Math.PI / 2);
  const flute = new THREE.Mesh(fluteGeometry, cutterMaterial);
  flute.position.z = tipLength + (fluteLength * 0.5);
  toolMarkerRoot.add(flute);

  const collarGeometry = new THREE.CylinderGeometry(radius * 0.5, radius * 0.5, collarLength, 22);
  collarGeometry.rotateX(Math.PI / 2);
  const collar = new THREE.Mesh(collarGeometry, accentMaterial);
  collar.position.z = tipLength + fluteLength + (collarLength * 0.5);
  toolMarkerRoot.add(collar);

  const shankGeometry = new THREE.CylinderGeometry(radius * 0.42, radius * 0.42, shankLength, 24);
  shankGeometry.rotateX(Math.PI / 2);
  const shank = new THREE.Mesh(shankGeometry, shankMaterial);
  shank.position.z = tipLength + fluteLength + collarLength + (shankLength * 0.5);
  toolMarkerRoot.add(shank);

  const capGeometry = new THREE.SphereGeometry(radius * 0.42, 20, 16);
  const cap = new THREE.Mesh(capGeometry, shankMaterial);
  cap.position.z = tipLength + fluteLength + collarLength + shankLength;
  toolMarkerRoot.add(cap);

  const haloGeometry = new THREE.TorusGeometry(radius * 0.9, radius * 0.08, 10, 36);
  const halo = new THREE.Mesh(haloGeometry, glowMaterial);
  halo.position.z = radius * 0.14;
  toolMarkerRoot.add(halo);

  const glowGeometry = new THREE.SphereGeometry(radius * 0.18, 16, 12);
  toolMarkerGlow = new THREE.Mesh(glowGeometry, glowMaterial.clone());
  toolMarkerGlow.position.z = radius * 0.08;
  toolMarkerRoot.add(toolMarkerGlow);

  machineMarker.add(toolMarkerRoot);
  toolMarkerInitialized = false;
}

function updateMachineMarker(state) {
  if (!toolMarkerRoot) {
    return;
  }

  const newMode = String(state.previewPlaybackMode ?? 'stopped');
  const currentLine = Number(state.currentLine ?? 0);
  const isPlaying = newMode !== 'stopped';
  const wasPlaying = jsPlay.mode !== 'stopped';

  toolMarkerRoot.visible = false;

  if (isPlaying && playbackKeyframes.length > 0) {
    const stepMs = Math.max(Number(state.previewPlaybackStepDurationMs ?? 83), 1);
    jsPlay.speed = 1000 / stepMs;
    jsPlay.direction = newMode === 'reverse' ? -1 : 1;
    jsPlay.mode = newMode;

    // Only (re)initialize from C#'s currentLine when genuinely starting fresh —
    // not when JS has simply frozen at the boundary waiting for C# to send 'stopped'.
    if ((!wasPlaying && !jsPlay.frozen) || !toolMarkerInitialized) {
      jsPlay.index = Math.max(0, findKeyframeIndex(currentLine));
      jsPlay.sub = 0;
      jsPlay.frozen = false;
      if (!toolMarkerInitialized) {
        const kf = playbackKeyframes[jsPlay.index];
        if (kf) {
          toolMarkerDisplay.set(kf.x, kf.y, kf.z);
          toolMarkerRoot.position.copy(toolMarkerDisplay);
        }
        toolMarkerInitialized = true;
      }
    }

    toolMarkerRoot.visible = true;
  } else {
    jsPlay.mode = 'stopped';
    jsPlay.frozen = false;
    jsPlay.sub = 0;

    const pos = resolveMarkerPosition(state);
    if (!pos) {
      toolMarkerInitialized = false;
      return;
    }

    toolMarkerRoot.visible = true;
    toolMarkerTarget.copy(pos);
    toolMarkerUseSmoothing = currentLine === 0;

    if (!toolMarkerInitialized) {
      toolMarkerDisplay.copy(pos);
      toolMarkerRoot.position.copy(pos);
      toolMarkerInitialized = true;
    } else if (currentLine > 0) {
      toolMarkerDisplay.copy(pos);
      toolMarkerRoot.position.copy(pos);
    }
  }
}

function resolveMarkerPosition(state) {
  if (!currentScene?.segments?.length) {
    return null;
  }

  const previewLine = Number(state.currentLine ?? 0);
  if (previewLine > 0) {
    return resolvePreviewLinePosition(previewLine);
  }

  return new THREE.Vector3(
    Number(state.positionX ?? 0),
    Number(state.positionY ?? 0),
    Number(state.positionZ ?? 0)
  );
}

function resolvePreviewLinePosition(previewLine) {
  const segments = currentScene?.segments ?? [];
  if (segments.length === 0) {
    return null;
  }

  let low = 0;
  let high = segments.length - 1;
  let matchIndex = -1;

  while (low <= high) {
    const mid = (low + high) >> 1;
    if (segments[mid].sourceLine <= previewLine) {
      matchIndex = mid;
      low = mid + 1;
    } else {
      high = mid - 1;
    }
  }

  if (matchIndex < 0) {
    const start = segments[0].start;
    return new THREE.Vector3(start[0], start[1], start[2]);
  }

  const end = segments[matchIndex].end;
  return new THREE.Vector3(end[0], end[1], end[2]);
}

function updateMarkerAnimation(delta) {
  if (!toolMarkerRoot || !toolMarkerRoot.visible || !toolMarkerInitialized) {
    return;
  }

  if (jsPlay.mode !== 'stopped' && playbackKeyframes.length > 1) {
    if (!jsPlay.frozen) {
      jsPlay.sub += delta * jsPlay.speed;

      while (jsPlay.sub >= 1) {
        jsPlay.sub -= 1;
        const next = jsPlay.index + jsPlay.direction;
        if (next < 0 || next >= playbackKeyframes.length) {
          jsPlay.sub = 0;
          jsPlay.frozen = true; // hold at boundary; C# will confirm stop shortly
          break;
        }
        jsPlay.index = next;
      }
    }

    const kf0 = playbackKeyframes[jsPlay.index];
    if (jsPlay.frozen) {
      toolMarkerDisplay.set(kf0.x, kf0.y, kf0.z);
      toolMarkerRoot.position.copy(toolMarkerDisplay);
      return;
    }
    const nextIdx = clamp(jsPlay.index + jsPlay.direction, 0, playbackKeyframes.length - 1);
    const kf1 = playbackKeyframes[nextIdx];
    interpolateAlongPath(kf0, kf1, jsPlay.sub, jsPlay.direction);
    return;
  }

  if (toolMarkerUseSmoothing) {
    const blend = 1 - Math.exp(-delta * 18);
    toolMarkerDisplay.lerp(toolMarkerTarget, blend);
    toolMarkerRoot.position.copy(toolMarkerDisplay);

    if (toolMarkerGlow) {
      const pulse = 0.92 + (Math.sin(performance.now() * 0.008) * 0.1);
      toolMarkerGlow.scale.setScalar(pulse);
      toolMarkerGlow.material.opacity = 0.48 + ((pulse - 0.92) * 1.6);
    }
  }
}

function disposeMarkerContents() {
  disposeNode(machineMarker);
  machineMarker.clear();
}

function disposeWorldContents() {
  for (const child of [...world.children]) {
    disposeRenderableNode(child);
  }

  world.clear();
}

function disposeRenderableNode(node) {
  for (const child of node.children ?? []) {
    disposeRenderableNode(child);
  }

  node.geometry?.dispose?.();

  if (Array.isArray(node.material)) {
    for (const material of node.material) {
      disposeRenderableMaterial(material);
    }
    return;
  }

  disposeRenderableMaterial(node.material);
}

function disposeRenderableMaterial(material) {
  if (!material || sharedToolpathMaterials.has(material)) {
    return;
  }

  material.dispose?.();
}

function disposeNode(node) {
  for (const child of node.children ?? []) {
    disposeNode(child);
  }

  node.geometry?.dispose?.();

  if (Array.isArray(node.material)) {
    for (const material of node.material) {
      material?.dispose?.();
    }
    return;
  }

  node.material?.dispose?.();
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
    return new THREE.Color(0xe0ae4b);
  }

  if (segment.isPlungeOrRetract) {
    return mixColor(0xbb74dd, 0xffc6ff, depth);
  }

  if (segment.isArc) {
    return mixColor(0x41c6bc, 0xa9e6ff, depth);
  }

  return mixColor(0x4d7fd2, 0xb6eeff, depth);
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

function interpolateAlongPath(kf0, kf1, t, direction) {
  // For forward: follow kf1.path (waypoints of the command ending at kf1) from start to end.
  // For reverse: follow kf0.path in reverse (waypoints of the command we're retreating through).
  const path = direction > 0 ? kf1.path : kf0.path;
  const arcStart = direction > 0 ? kf1 : kf0;  // path endpoint (kf for p0 fallback at si=0)
  const arcOrigin = direction > 0 ? kf0 : kf1; // where the arc started (used when si=0)
  const n = path ? path.length : 0;

  let px, py, pz;

  if (n === 0) {
    // No path data: straight lerp
    px = kf0.x + (kf1.x - kf0.x) * t;
    py = kf0.y + (kf1.y - kf0.y) * t;
    pz = kf0.z + (kf1.z - kf0.z) * t;
  } else if (direction > 0) {
    // Forward: map t from 0→1 across path[0..n-1], with arcOrigin as the implicit start
    const raw = t * n;
    const si = Math.min(Math.floor(raw), n - 1);
    const st = raw - si;
    const p0 = si === 0 ? arcOrigin : path[si - 1];
    const p1 = path[si];
    px = p0.x + (p1.x - p0.x) * st;
    py = p0.y + (p1.y - p0.y) * st;
    pz = p0.z + (p1.z - p0.z) * st;
  } else {
    // Reverse: traverse kf0.path backwards (from its last waypoint toward its first).
    // At t=0 we're at kf0 (= path[n-1]); at t=1 we arrive at kf1 (= path start's origin).
    const raw = (1 - t) * n;
    const si = Math.min(Math.floor(raw), n - 1);
    const st = raw - si;
    const p0 = si === 0 ? arcOrigin : path[si - 1];
    const p1 = path[si];
    px = p0.x + (p1.x - p0.x) * st;
    py = p0.y + (p1.y - p0.y) * st;
    pz = p0.z + (p1.z - p0.z) * st;
  }

  toolMarkerDisplay.set(px, py, pz);
  toolMarkerRoot.position.copy(toolMarkerDisplay);
}

function buildPlaybackKeyframes(segments) {
  const frames = [];
  if (!segments || segments.length === 0) {
    return frames;
  }

  // Keyframe 0: the initial machine position before any motion
  frames.push({ x: segments[0].start[0], y: segments[0].start[1], z: segments[0].start[2], sourceLine: 0, path: [] });

  // One keyframe per unique source line.
  // path[] holds the end-point of every tessellated segment for that command,
  // in order — the marker follows this path instead of taking a straight chord.
  let prevLine = -1;
  let pendingPath = [];

  for (const seg of segments) {
    if (seg.sourceLine !== prevLine) {
      if (pendingPath.length > 0) {
        const last = pendingPath[pendingPath.length - 1];
        frames.push({ x: last.x, y: last.y, z: last.z, sourceLine: prevLine, path: pendingPath });
      }
      prevLine = seg.sourceLine;
      pendingPath = [];
    }
    pendingPath.push({ x: seg.end[0], y: seg.end[1], z: seg.end[2] });
  }

  if (pendingPath.length > 0) {
    const last = pendingPath[pendingPath.length - 1];
    frames.push({ x: last.x, y: last.y, z: last.z, sourceLine: prevLine, path: pendingPath });
  }

  return frames;
}

function findKeyframeIndex(sourceLine) {
  if (playbackKeyframes.length === 0) {
    return 0;
  }

  let lo = 0, hi = playbackKeyframes.length - 1, result = 0;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (playbackKeyframes[mid].sourceLine <= sourceLine) {
      result = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return result;
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

  updateMarkerAnimation(delta);
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

function handleThemeChanged() {
  applyViewerTheme();
  if (currentScene) {
    buildScene();
    if (currentState) {
      applyState();
    }
  }
}

function applyViewerTheme() {
  renderer.setClearColor(getThemeColors().clearColor, 1);
}

function getThemeColors() {
  return themeQuery.matches
    ? {
        clearColor: 0x161616,
        gridPrimary: 0x25313d,
        gridSecondary: 0x1e2934,
        stockBox: 0x3a5367
      }
    : {
        clearColor: 0xe9eef4,
        gridPrimary: 0x9fb2c7,
        gridSecondary: 0xc4cfdb,
        stockBox: 0x7a90a7
      };
}

async function fetchJson(url) {
  const sep = url.includes('?') ? '&' : '?';
  const response = await fetch(`${url}${sep}_=${Date.now()}`, { cache: 'no-store' });
  if (!response.ok) {
    return null;
  }

  return await response.json();
}
