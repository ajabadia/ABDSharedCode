/**
 * ════════════════════════════════════════════════════════════════════════════════
 * ABD STUDIO TOPOLOGY — Motor Interactivo de Nodos & Cables Bézier
 * Adaptado con física de gravedad (sag) y mazos paralelos de ABDOmegaUnified
 * ════════════════════════════════════════════════════════════════════════════════
 */

// Constantes de física de cables (de ABDOmegaUnified y catenaria natural)
const CABLE_PHYSICS = {
    BASE_SAG: 85,
    SAG_FACTOR: 0.22,
    MAX_SAG: 260,
    STROKE_WIDTH: 4,
    PLUG_RADIUS: 5,
    SPREAD: 16 // Separación de cables paralelos en el mazo
};

const SIGNAL_COLORS = {
    'audioOut': '#f59e0b', // Ámbar (Audio Probe/Stimulus)
    'audioIn':  '#10b981', // Verde esmeralda (Audio Return)
    'midiOut':  '#a855f7', // Púrpura (MIDI Out)
    'midiIn':   '#06b6d4'  // Cian (MIDI In)
};

// Estado global de topología
let topologyData = {
    target: null,
    devices: [],
    connections: []
};

const nodePositions = new Map(); // id -> { x, y }
let activeDrag = null; // { id, startX, startY, origNodeX, origNodeY }

/**
 * Cálculo de separación lateral de cables paralelos en mazo (de ABDOmegaUnified)
 */
function computeBundleSpread(x1, y1, x2, y2, position, groupSize) {
    if (groupSize <= 1) return { x: 0, y: 0 };
    const dx = x2 - x1;
    const dy = y2 - y1;
    const length = Math.hypot(dx, dy) || 1;

    // Normal perpendicular al eje
    const nx = -dy / length;
    const ny = dx / length;

    const offset = (position - (groupSize - 1) / 2) * CABLE_PHYSICS.SPREAD;
    return { x: nx * offset, y: ny * offset };
}

/**
 * Cálculo del path Bézier cúbico con comba por gravedad (sag) y caída vertical suave
 */
function calculateCablePath(x1, y1, x2, y2, position, groupSize) {
    const dx = x2 - x1;
    const dy = y2 - y1;
    const dist = Math.hypot(dx, dy);

    // Sag gravitatorio proporcional a la distancia con caída generosa
    const sag = Math.min(CABLE_PHYSICS.BASE_SAG + dist * CABLE_PHYSICS.SAG_FACTOR, CABLE_PHYSICS.MAX_SAG);
    const spread = computeBundleSpread(x1, y1, x2, y2, position, groupSize);

    // Tangentes de salida verticales (+Y) para simular peso del cable colgado de la clavija
    const vertDrop = Math.max(30, sag * 0.45);

    const cx1 = x1 + spread.x;
    const cy1 = y1 + vertDrop + sag * 0.7 + spread.y;
    const cx2 = x2 + spread.x;
    const cy2 = y2 + vertDrop + sag * 0.7 + spread.y;

    return `M ${x1} ${y1} C ${cx1} ${cy1}, ${cx2} ${cy2}, ${x2} ${y2}`;
}

/**
 * Inicializar / Actualizar la Topología
 */
window.updateTopology = function(data) {
    if (!data) return;
    topologyData = data;
    renderStudio();
};

function renderStudio() {
    const container = document.getElementById('canvas-container');
    const nodesLayer = document.getElementById('nodes-layer');
    nodesLayer.innerHTML = '';

    const width = container.clientWidth || window.innerWidth;
    const height = container.clientHeight || window.innerHeight;

    const centerX = width / 2;
    const centerY = height / 2 + 10;

    // Storage key for user-dragged node layout
    const STORAGE_KEY = 'abd_studio_topology_positions_v1';
    let savedPositions = {};
    try {
        const raw = localStorage.getItem(STORAGE_KEY);
        if (raw) savedPositions = JSON.parse(raw);
    } catch (_) {}

    // 1. Posicionar Target en el centro (o posición guardada)
    if (topologyData.target) {
        if (!nodePositions.has('target')) {
            if (savedPositions['target'] && typeof savedPositions['target'].x === 'number') {
                nodePositions.set('target', { x: savedPositions['target'].x, y: savedPositions['target'].y });
            } else {
                nodePositions.set('target', { x: centerX, y: centerY + 15 });
            }
        }
    }

    // 2. Posicionar dispositivos distribuidos en ESQUINAS / LATERALES (evitar eje vertical directo sobre el target)
    const devices = topologyData.devices || [];
    const connectedDevs = devices.filter(d => d.assigned);
    const unassignedDevs = devices.filter(d => !d.assigned);

    // Esquinas y flancos para dispositivos conectados:
    // Evitamos el ángulo central superior (-Math.PI/2) para que ningún interface quede justo encima del Target
    // 0: Arriba Izquierda (Top-Left), 1: Arriba Derecha (Top-Right), 2: Lateral Izquierdo, 3: Lateral Derecho
    const cornerSlots = [
        { x: width * 0.22, y: height * 0.28 }, // Arriba Izquierda
        { x: width * 0.78, y: height * 0.28 }, // Arriba Derecha
        { x: width * 0.16, y: height * 0.58 }, // Flanco Izquierdo
        { x: width * 0.84, y: height * 0.58 }, // Flanco Derecho
        { x: width * 0.32, y: height * 0.24 }, // Flanco Superior Izquierdo
        { x: width * 0.68, y: height * 0.24 }  // Flanco Superior Derecho
    ];

    connectedDevs.forEach((dev, idx) => {
        if (!nodePositions.has(dev.id)) {
            if (savedPositions[dev.id] && typeof savedPositions[dev.id].x === 'number') {
                nodePositions.set(dev.id, { x: savedPositions[dev.id].x, y: savedPositions[dev.id].y });
            } else {
                const slot = cornerSlots[idx % cornerSlots.length];
                nodePositions.set(dev.id, {
                    x: Math.max(120, Math.min(width - 120, slot.x)),
                    y: Math.max(100, Math.min(height - 100, slot.y))
                });
            }
        }
    });

    // Dispositivos inactivos / no asignados: fila o arco inferior
    const unassignedSlots = [
        { x: width * 0.20, y: height * 0.82 },
        { x: width * 0.50, y: height * 0.84 },
        { x: width * 0.80, y: height * 0.82 },
        { x: width * 0.35, y: height * 0.86 },
        { x: width * 0.65, y: height * 0.86 }
    ];

    unassignedDevs.forEach((dev, idx) => {
        if (!nodePositions.has(dev.id)) {
            if (savedPositions[dev.id] && typeof savedPositions[dev.id].x === 'number') {
                nodePositions.set(dev.id, { x: savedPositions[dev.id].x, y: savedPositions[dev.id].y });
            } else {
                const slot = unassignedSlots[idx % unassignedSlots.length];
                nodePositions.set(dev.id, {
                    x: Math.max(120, Math.min(width - 120, slot.x)),
                    y: Math.max(100, Math.min(height - 100, slot.y))
                });
            }
        }
    });

    // 3. Detección y resolución de colisiones entre nodos (Relaxation Pass)
    const allNodeKeys = Array.from(nodePositions.keys());
    const minSeparationX = 250; // Ancho nodo (~180-230px) + margen
    const minSeparationY = 200; // Alto nodo (~140-180px) + margen

    for (let iter = 0; iter < 10; ++iter) {
        for (let i = 0; i < allNodeKeys.length; ++i) {
            for (let j = i + 1; j < allNodeKeys.length; ++j) {
                const idA = allNodeKeys[i];
                const idB = allNodeKeys[j];
                const posA = nodePositions.get(idA);
                const posB = nodePositions.get(idB);

                const dx = posB.x - posA.x;
                const dy = posB.y - posA.y;

                const absX = Math.abs(dx);
                const absY = Math.abs(dy);

                if (absX < minSeparationX && absY < minSeparationY) {
                    const overlapX = minSeparationX - absX;
                    const overlapY = minSeparationY - absY;

                    const shiftFactor = 0.5;
                    const signX = dx >= 0 ? 1 : -1;
                    const signY = dy >= 0 ? 1 : -1;

                    // Si idA o idB es target, preferir mover el otro nodo para mantener centrado el hero
                    if (idA === 'target') {
                        posB.x += signX * overlapX;
                        posB.y += signY * overlapY;
                    } else if (idB === 'target') {
                        posA.x -= signX * overlapX;
                        posA.y -= signY * overlapY;
                    } else {
                        posA.x -= signX * overlapX * shiftFactor;
                        posA.y -= signY * overlapY * shiftFactor;
                        posB.x += signX * overlapX * shiftFactor;
                        posB.y += signY * overlapY * shiftFactor;
                    }
                }
            }
        }
    }

    // 4. Renderizar nodos con posiciones ajustadas
    if (topologyData.target) {
        renderNode('target', topologyData.target, true);
    }
    connectedDevs.forEach(dev => renderNode(dev.id, dev, false));
    unassignedDevs.forEach(dev => renderNode(dev.id, dev, false));

    // Forzar renderizado y lectura de geometrías tras inserción DOM
    requestAnimationFrame(() => {
        updateCables();
    });
}

function renderNode(id, dev, isTarget) {
    const nodesLayer = document.getElementById('nodes-layer');
    const pos = nodePositions.get(id);

    const nodeEl = document.createElement('div');
    nodeEl.id = `node-${id}`;
    nodeEl.className = `device-node ${isTarget ? 'target-hero' : 'interface-node'} ${!isTarget && !dev.assigned ? 'unassigned' : ''}`;
    nodeEl.style.left = `${pos.x}px`;
    nodeEl.style.top = `${pos.y}px`;

    // Badge
    const badge = document.createElement('div');
    badge.className = `node-badge ${isTarget ? 'target-badge' : (dev.assigned ? 'connected' : 'unassigned-badge')}`;
    badge.textContent = isTarget ? 'TARGET DE MEDICIÓN' : (dev.assigned ? 'CONECTADA' : 'NO ASIGNADA');
    nodeEl.appendChild(badge);

    // Imagen
    const imgContainer = document.createElement('div');
    imgContainer.className = 'node-image-container';
    const img = document.createElement('img');
    img.className = 'node-image';
    img.src = dev.image || 'models/generic-digital-keyboard.png';
    img.alt = dev.name;
    img.onerror = () => {
        img.src = isTarget ? 'models/generic-digital-keyboard.png' : 'interfaces/generic-audio-midi-interface.png';
    };
    imgContainer.appendChild(img);
    nodeEl.appendChild(imgContainer);

    // Título y categoría
    const title = document.createElement('div');
    title.className = 'node-title';
    title.textContent = dev.name;
    title.title = dev.name;
    nodeEl.appendChild(title);

    const subtitle = document.createElement('div');
    subtitle.className = 'node-subtitle';
    subtitle.textContent = dev.details || dev.category || (isTarget ? 'Hardware Bajo Prueba' : 'Dispositivo I/O');
    nodeEl.appendChild(subtitle);

    // Conectores Jacks: Si el dispositivo provee lista de puertos (Mac Audio/MIDI Setup style), renderizar cada puerto
    const jacks = document.createElement('div');
    jacks.className = 'node-jacks';

    if (dev.ports && Array.isArray(dev.ports) && dev.ports.length > 0) {
        dev.ports.forEach(port => {
            const jackEl = document.createElement('div');
            const typeClass = (port.type || 'audio-out').replace(/([A-Z])/g, '-$1').toLowerCase();
            jackEl.className = `jack-point ${typeClass} ${port.connected ? 'active' : ''}`;
            jackEl.title = `${port.name || port.id} (${port.type})`;
            jackEl.id = `jack-${id}-${port.id}`;
            jacks.appendChild(jackEl);
        });
    } else {
        // Fallback clásico: 1 par Audio, 1 par MIDI si aplica
        const jackTypes = [];
        if (dev.hasAudio !== false) {
            jackTypes.push({ type: 'audioOut', title: 'Audio Out' });
            jackTypes.push({ type: 'audioIn',  title: 'Audio In' });
        }
        if (dev.hasMidi !== false) {
            jackTypes.push({ type: 'midiOut',  title: 'MIDI Out' });
            jackTypes.push({ type: 'midiIn',   title: 'MIDI In' });
        }

        jackTypes.forEach(j => {
            const jackEl = document.createElement('div');
            jackEl.className = `jack-point ${j.type.replace(/([A-Z])/g, '-$1').toLowerCase()}`;
            jackEl.title = j.title;
            jackEl.id = `jack-${id}-${j.type}`;
            jacks.appendChild(jackEl);
        });
    }

    nodeEl.appendChild(jacks);

    // Drag & Drop con Pointer Events fluido
    nodeEl.addEventListener('pointerdown', (e) => {
        if (e.button !== 0) return;
        activeDrag = {
            id: id,
            startX: e.clientX,
            startY: e.clientY,
            origNodeX: pos.x,
            origNodeY: pos.y
        };
        nodeEl.classList.add('dragging');
        nodeEl.setPointerCapture(e.pointerId);
        e.stopPropagation();
    });

    nodeEl.addEventListener('pointermove', (e) => {
        if (!activeDrag || activeDrag.id !== id) return;
        const dx = e.clientX - activeDrag.startX;
        const dy = e.clientY - activeDrag.startY;

        const newX = Math.max(80, Math.min(window.innerWidth - 80, activeDrag.origNodeX + dx));
        const newY = Math.max(70, Math.min(window.innerHeight - 70, activeDrag.origNodeY + dy));

        pos.x = newX;
        pos.y = newY;
        nodeEl.style.left = `${newX}px`;
        nodeEl.style.top = `${newY}px`;

        updateCables();
    });

    const endDrag = (e) => {
        if (activeDrag && activeDrag.id === id) {
            nodeEl.classList.remove('dragging');
            try { nodeEl.releasePointerCapture(e.pointerId); } catch (_) {}
            activeDrag = null;

            // Persist user positions across sessions/reopenings
            try {
                const raw = localStorage.getItem(STORAGE_KEY);
                const current = raw ? JSON.parse(raw) : {};
                current[id] = { x: pos.x, y: pos.y };
                localStorage.setItem(STORAGE_KEY, JSON.stringify(current));
            } catch (_) {}
        }
    };

    nodeEl.addEventListener('pointerup', endDrag);
    nodeEl.addEventListener('pointercancel', endDrag);

    nodesLayer.appendChild(nodeEl);
}

function updateCables() {
    const cablesGroup = document.getElementById('cables-group');
    const plugsGroup = document.getElementById('plugs-group');
    if (!cablesGroup || !plugsGroup) return;

    cablesGroup.innerHTML = '';
    plugsGroup.innerHTML = '';

    const connections = topologyData.connections || [];

    // Agrupar conexiones por pares de nodos para el bundle spread
    const pairGroups = new Map();
    connections.forEach((conn, idx) => {
        const pairKey = [conn.from, conn.to].sort().join('<->');
        if (!pairGroups.has(pairKey)) pairGroups.set(pairKey, []);
        pairGroups.get(pairKey).push({ conn, idx });
    });

    pairGroups.forEach((group) => {
        const groupSize = group.length;

        group.forEach(({ conn }, posInGroup) => {
            const pFrom = nodePositions.get(conn.from);
            const pTo = nodePositions.get(conn.to);
            if (!pFrom || !pTo) return;

            // Búsqueda inteligente de jacks: 
            // 1) por ID de puerto exacto (e.g. jack-dev_0-audio_out o jack-dev_0-midi_in_0)
            // 2) por tipo general (e.g. jack-target-audioOut)
            const jackFromEl = (conn.fromPort ? document.getElementById(`jack-${conn.from}-${conn.fromPort}`) : null) 
                            || document.getElementById(`jack-${conn.from}-${conn.type}`);
            const jackToEl   = (conn.toPort ? document.getElementById(`jack-${conn.to}-${conn.toPort}`) : null)
                            || document.getElementById(`jack-${conn.to}-${conn.type}`);

            let x1 = pFrom.x, y1 = pFrom.y + 40;
            let x2 = pTo.x,   y2 = pTo.y + 40;

            if (jackFromEl) {
                const r = jackFromEl.getBoundingClientRect();
                x1 = r.left + r.width / 2;
                y1 = r.top + r.height / 2;
                jackFromEl.classList.add('active');
            }
            if (jackToEl) {
                const r = jackToEl.getBoundingClientRect();
                x2 = r.left + r.width / 2;
                y2 = r.top + r.height / 2;
                jackToEl.classList.add('active');
            }

            const pathD = calculateCablePath(x1, y1, x2, y2, posInGroup, groupSize);
            const color = SIGNAL_COLORS[conn.type] || '#38bdf8';

            // 1. Path del Cable con curva y sombra
            const pathEl = document.createElementNS('http://www.w3.org/2000/svg', 'path');
            pathEl.setAttribute('d', pathD);
            pathEl.setAttribute('class', 'patch-cable pulse-anim');
            pathEl.setAttribute('stroke', color);
            pathEl.setAttribute('filter', 'url(#cable-shadow)');
            cablesGroup.appendChild(pathEl);

            // 2. Plugs / Jacks en los extremos
            [ { x: x1, y: y1 }, { x: x2, y: y2 } ].forEach(pt => {
                const circle = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
                circle.setAttribute('cx', pt.x);
                circle.setAttribute('cy', pt.y);
                circle.setAttribute('r', CABLE_PHYSICS.PLUG_RADIUS);
                circle.setAttribute('class', 'cable-plug');
                circle.setAttribute('stroke', color);
                plugsGroup.appendChild(circle);
            });
        });
    });
}

// Redibujar al cambiar tamaño de ventana
window.addEventListener('resize', () => {
    if (topologyData.target) {
        updateCables();
    }
});
