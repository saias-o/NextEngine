/* ==========================================================================
   NextEngine Retro-Futuristic Script (Vanilla JS)
   ========================================================================== */

// --- Global State ---
const state = {
    soundEnabled: false,
    crtEnabled: true,
    activeTab: 'home',
    donations: 1402.50,
    fps: 60.00,
    vertices: 6272
};

// --- Web Audio API Synth Engine ---
let audioCtx = null;
let ambientOsc1 = null;
let ambientOsc2 = null;
let ambientFilter = null;
let ambientGain = null;
let lfo = null;

function initAudio() {
    if (audioCtx) return;
    
    // Create audio context
    audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    
    // Create components for Ambient Synth Pad
    ambientOsc1 = audioCtx.createOscillator();
    ambientOsc2 = audioCtx.createOscillator();
    ambientFilter = audioCtx.createBiquadFilter();
    ambientGain = audioCtx.createGain();
    
    // Tune oscillators (slightly detuned low triangle waves for nice chorus drone)
    ambientOsc1.type = 'triangle';
    ambientOsc1.frequency.value = 110.0; // A2
    
    ambientOsc2.type = 'triangle';
    ambientOsc2.frequency.value = 110.5; // Slightly detuned
    
    // Setup Lowpass filter to keep it deep and soft
    ambientFilter.type = 'lowpass';
    ambientFilter.frequency.value = 240; 
    ambientFilter.Q.value = 3.0;
    
    // Setup LFO to modulate filter cutoff (breathing ambient effect)
    lfo = audioCtx.createOscillator();
    const lfoGain = audioCtx.createGain();
    lfo.type = 'sine';
    lfo.frequency.value = 0.15; // Slow breathing rate (0.15 Hz)
    lfoGain.gain.value = 60; // Modulate filter between 180Hz and 300Hz
    
    lfo.connect(lfoGain);
    lfoGain.connect(ambientFilter.frequency);
    
    // Connect pad nodes
    ambientOsc1.connect(ambientFilter);
    ambientOsc2.connect(ambientFilter);
    ambientFilter.connect(ambientGain);
    ambientGain.connect(audioCtx.destination);
    
    // Initial silent state
    ambientGain.gain.value = 0.0;
    
    // Start ambient oscillators
    ambientOsc1.start(0);
    ambientOsc2.start(0);
    lfo.start(0);
}

function playHoverSound() {
    if (!state.soundEnabled || !audioCtx) return;
    
    try {
        const osc = audioCtx.createOscillator();
        const gain = audioCtx.createGain();
        osc.type = 'triangle';
        osc.frequency.setValueAtTime(150, audioCtx.currentTime);
        osc.frequency.exponentialRampToValueAtTime(70, audioCtx.currentTime + 0.05);
        
        gain.gain.setValueAtTime(0.04, audioCtx.currentTime);
        gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + 0.05);
        
        osc.connect(gain);
        gain.connect(audioCtx.destination);
        osc.start();
        osc.stop(audioCtx.currentTime + 0.06);
    } catch(e) { console.warn(e); }
}

function playClickSound() {
    if (!state.soundEnabled || !audioCtx) return;
    
    try {
        const osc = audioCtx.createOscillator();
        const gain = audioCtx.createGain();
        osc.type = 'square';
        osc.frequency.setValueAtTime(450, audioCtx.currentTime);
        osc.frequency.setValueAtTime(900, audioCtx.currentTime + 0.03);
        
        gain.gain.setValueAtTime(0.03, audioCtx.currentTime);
        gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + 0.08);
        
        osc.connect(gain);
        gain.connect(audioCtx.destination);
        osc.start();
        osc.stop(audioCtx.currentTime + 0.09);
    } catch(e) { console.warn(e); }
}

function playDonateCoinSound() {
    if (!state.soundEnabled || !audioCtx) return;
    
    try {
        const osc = audioCtx.createOscillator();
        const gain = audioCtx.createGain();
        
        osc.type = 'square';
        
        // Classic 8-bit Coin arpeggio: short B5 then sustained E6
        osc.frequency.setValueAtTime(987.77, audioCtx.currentTime); // B5
        osc.frequency.setValueAtTime(1318.51, audioCtx.currentTime + 0.08); // E6
        
        gain.gain.setValueAtTime(0.06, audioCtx.currentTime);
        gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + 0.35);
        
        osc.connect(gain);
        gain.connect(audioCtx.destination);
        osc.start();
        osc.stop(audioCtx.currentTime + 0.4);
    } catch(e) { console.warn(e); }
}

function toggleAmbientPad(enable) {
    if (!audioCtx) return;
    
    const targetGain = enable ? 0.15 : 0.0;
    ambientGain.gain.setValueAtTime(ambientGain.gain.value, audioCtx.currentTime);
    ambientGain.gain.linearRampToValueAtTime(targetGain, audioCtx.currentTime + 1.0); // 1-second fade
}


// --- 3D WebGL Pixel Engine (Three.js) ---
let scene, camera, renderer, waveGroup, starsGroup;
let wavePoints = [];
const canvasWidth = 480;
const canvasHeight = 480;

function init3D() {
    const container = document.getElementById('threejs-container');
    if (!container) return;
    
    // Create Scene & Camera
    scene = new THREE.Scene();
    scene.fog = new THREE.FogExp2(0x060813, 0.02);
    
    camera = new THREE.PerspectiveCamera(45, canvasWidth / canvasHeight, 0.1, 1000);
    camera.position.z = 10;
    
    // Renderer (higher resolution with antialiasing for modern, crisp styling)
    renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    renderer.setSize(canvasWidth, canvasHeight);
    renderer.setPixelRatio(window.devicePixelRatio || 1);
    
    // Apply modern styling to canvas directly
    const canvas = renderer.domElement;
    canvas.style.width = '100%';
    canvas.style.height = '100%';
    canvas.style.objectFit = 'contain';
    
    container.appendChild(canvas);
    
    // Create Cosmic Wave Group
    waveGroup = new THREE.Group();
    scene.add(waveGroup);
    
    // Build 28x28 Wave Plane
    const rows = 28;
    const cols = 28;
    const spacing = 0.25;
    
    for (let r = 0; r < rows; r++) {
        for (let c = 0; c < cols; c++) {
            // Sleek tiny point particles
            const geo = new THREE.BoxGeometry(0.018, 0.018, 0.018);
            
            // Emerald green to Mint space wave gradient
            const colorRatio = r / rows;
            const color = new THREE.Color().lerpColors(
                new THREE.Color(0x10b981), // Emerald
                new THREE.Color(0x34d399), // Mint Green
                colorRatio
            );
            
            const mat = new THREE.MeshBasicMaterial({ color: color });
            const mesh = new THREE.Mesh(geo, mat);
            
            const x = (r - rows / 2) * spacing;
            const z = (c - cols / 2) * spacing;
            mesh.position.set(x, 0, z);
            
            waveGroup.add(mesh);
            wavePoints.push({ mesh, x, z });
        }
    }
    
    // Twinkling Space Dust Stars
    starsGroup = new THREE.Group();
    const starGeo = new THREE.BoxGeometry(0.01, 0.01, 0.01);
    const starMat = new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.8 });
    
    for (let i = 0; i < 180; i++) {
        const star = new THREE.Mesh(starGeo, starMat);
        const radius = 6.5 + Math.random() * 7.5;
        const theta = Math.random() * Math.PI * 2;
        const phi = Math.acos((Math.random() * 2) - 1);
        
        star.position.set(
            radius * Math.sin(phi) * Math.cos(theta),
            radius * Math.sin(phi) * Math.sin(theta),
            radius * Math.cos(phi)
        );
        
        star.userData = { twinkleSpeed: 0.8 + Math.random() * 2.5 };
        starsGroup.add(star);
    }
    scene.add(starsGroup);
    
    // Mouse interaction tracking
    let targetRotationX = 0;
    let targetRotationY = 0;
    
    window.addEventListener('mousemove', (e) => {
        const mouseX = (e.clientX / window.innerWidth) - 0.5;
        const mouseY = (e.clientY / window.innerHeight) - 0.5;
        targetRotationX = mouseY * 0.6;
        targetRotationY = mouseX * 0.6;
    });
    
    // Animation loop
    const clock = new THREE.Clock();
    
    function animate() {
        requestAnimationFrame(animate);
        
        const delta = clock.getDelta();
        const elapsed = clock.getElapsedTime();
        
        // Apply camera perspective tilt and continuous slow spin
        waveGroup.rotation.x = -0.7 + (targetRotationX - waveGroup.rotation.x) * 0.08;
        waveGroup.rotation.y = elapsed * 0.06 + (targetRotationY - waveGroup.rotation.y) * 0.08;
        
        // Compute Double Sine wave equations for each pixel node in the plane
        const timeFactor = elapsed * 2.2;
        wavePoints.forEach(b => {
            const dist = Math.sqrt(b.x * b.x + b.z * b.z);
            const y = Math.sin(dist * 0.95 - timeFactor) * Math.cos(b.x * 0.45 + elapsed * 1.1) * 0.65;
            b.mesh.position.y = y;
            
            // Slight height-based scale pulsing for added energy glow
            const scale = 1.0 + y * 0.25;
            b.mesh.scale.set(scale, scale, scale);
        });
        
        // Background Space twinkling animation
        starsGroup.rotation.y = elapsed * 0.015;
        starsGroup.children.forEach(star => {
            const pulse = 0.5 + Math.abs(Math.sin(elapsed * star.userData.twinkleSpeed)) * 0.8;
            star.scale.set(pulse, pulse, pulse);
        });
        
        // Fluctuating Diagnostics Counters
        updateDiagnostics();
        
        renderer.render(scene, camera);
    }
    
    animate();
}

function updateDiagnostics() {
    // FPS fluctuation
    state.fps = 59.50 + Math.sin(Date.now() / 1000) * 0.60 + Math.random() * 0.10;
    const fpsText = document.getElementById('stat-fps');
    if (fpsText) fpsText.innerText = `FPS: ${state.fps.toFixed(2)}`;
    
    // Vertex simulation count shifting when camera angles rotate
    const vertexText = document.getElementById('stat-poly');
    if (vertexText) {
        const pulseVerts = state.vertices + Math.floor(Math.sin(Date.now() / 500) * 12);
        vertexText.innerText = `VERTICES: ${pulseVerts}`;
    }
}


// --- GitHub Releases Changelog Fetcher & Renderer ---
let changelogLoaded = false;

function escapeHTML(str) {
    return str
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#039;");
}

function parseMarkdown(mdText) {
    if (!mdText) return 'No description provided.';
    let html = escapeHTML(mdText);
    
    // Replace headings: ### text -> <h4>text</h4>, ## text -> <h3>text</h3>
    html = html.replace(/^### (.*?)$/gm, '<h4 style="color:var(--color-accent-primary);margin:10px 0 5px 0;">$1</h4>');
    html = html.replace(/^## (.*?)$/gm, '<h3 style="color:var(--color-accent-primary);margin:12px 0 6px 0;">$1</h3>');
    html = html.replace(/^# (.*?)$/gm, '<h2 style="color:var(--color-accent-primary);margin:15px 0 8px 0;">$1</h2>');
    
    // Replace bold: **text** -> <strong>text</strong>
    html = html.replace(/\*\*(.*?)\*\*/g, '<strong style="color:#fff;">$1</strong>');
    
    // Replace inline code: `code` -> <code style="color:var(--color-accent-emerald);background:rgba(0,0,0,0.4);padding:2px 4px;border-radius:3px;">code</code>
    html = html.replace(/`(.*?)`/g, '<code style="color:var(--color-accent-emerald);background:rgba(0,0,0,0.4);padding:2px 4px;border-radius:3px;font-family:var(--font-mono);">$1</code>');
    
    // Replace list items: - item -> • item
    html = html.replace(/^[\-\*]\s+(.*?)$/gm, '<div style="padding-left:12px;position:relative;"><span style="color:var(--color-accent-rose);position:absolute;left:0;">•</span> $1</div>');
    
    return html;
}

async function loadChangelog() {
    if (changelogLoaded) return;
    
    const container = document.getElementById('changelog-container');
    if (!container) return;
    
    try {
        const response = await fetch('https://api.github.com/repos/saias-o/NextEngine/releases');
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        const data = await response.json();
        
        if (!data || data.length === 0) {
            showEmptyChangelog();
            return;
        }
        
        renderReleases(data);
        changelogLoaded = true;
    } catch (error) {
        console.error('Failed to fetch GitHub releases:', error);
        showEmptyChangelog();
    }
}

function renderReleases(releases) {
    const container = document.getElementById('changelog-container');
    if (!container) return;
    
    container.innerHTML = '';
    
    releases.forEach(release => {
        const card = document.createElement('div');
        card.className = 'changelog-card';
        
        const dateStr = new Date(release.published_at).toLocaleDateString('en-US', {
            year: 'numeric',
            month: 'short',
            day: 'numeric'
        });
        
        card.innerHTML = `
            <div class="changelog-header">
                <span class="changelog-tag">${release.tag_name}</span>
                <span class="changelog-date">${dateStr}</span>
            </div>
            <h3 class="changelog-title">${release.name || release.tag_name}</h3>
            <div class="changelog-body">${parseMarkdown(release.body)}</div>
            <div class="changelog-footer">
                <a href="${release.html_url}" target="_blank" rel="noopener" class="changelog-git-btn">VIEW ON GITHUB ➔</a>
            </div>
        `;
        
        container.appendChild(card);
    });
}

function showEmptyChangelog() {
    const container = document.getElementById('changelog-container');
    if (!container) return;
    
    container.innerHTML = `
        <div class="error-state">
            <span class="pulse-core" style="color: var(--color-accent-primary);">NO RELEASES YET</span>
        </div>
    `;
}

// --- SPA Page Transitions ---
function initNavigation() {
    const navItems = document.querySelectorAll('.nav-item[data-tab]');
    const tabPanels = document.querySelectorAll('.tab-panel');
    
    navItems.forEach(item => {
        item.addEventListener('click', (e) => {
            e.preventDefault();
            const tabId = item.getAttribute('data-tab');
            switchTab(tabId);
        });
        
        item.addEventListener('mouseenter', () => {
            playHoverSound();
        });
    });
    
    // Inter-page transition triggers (buttons inside panels)
    document.querySelectorAll('.start-transition').forEach(btn => {
        btn.addEventListener('click', () => {
            const target = btn.getAttribute('data-target');
            switchTab(target);
        });
        btn.addEventListener('mouseenter', () => playHoverSound());
    });
}

function switchTab(tabId) {
    if (state.activeTab === tabId) return;
    
    playClickSound();
    
    state.activeTab = tabId;
    
    if (tabId === 'changelog') {
        loadChangelog();
    }
    
    // Update navigation styles
    document.querySelectorAll('.nav-item').forEach(btn => {
        if (btn.getAttribute('data-tab') === tabId) {
            btn.classList.add('active');
        } else {
            btn.classList.remove('active');
        }
    });
    
    // Hide/Show Panels
    document.querySelectorAll('.tab-panel').forEach(panel => {
        if (panel.getAttribute('id') === `sec-${tabId}`) {
            panel.classList.add('active');
        } else {
            panel.classList.remove('active');
        }
    });
    
    // Scroll content to top
    document.getElementById('main-content').scrollTop = 0;
}


// --- Interactive CLI Terminal Console ---
function initTerminal() {
    const cliInput = document.getElementById('terminal-cli');
    const screen = document.getElementById('terminal-screen');
    
    if (!cliInput || !screen) return;
    
    // Handle Command Submissions
    cliInput.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') {
            const cmd = cliInput.value.trim().toLowerCase();
            cliInput.value = '';
            
            if (cmd) {
                playClickSound();
                processCommand(cmd);
            }
        }
    });
    
    // Auto-focus input on clicking anywhere inside the console screen
    document.getElementById('terminal-screen').parentElement.addEventListener('click', () => {
        cliInput.focus();
    });
}

function processCommand(cmd) {
    const screen = document.getElementById('terminal-screen');
    
    // Echo the command typed
    appendConsoleLine(`guest@nextengine:~# ${cmd}`, 'text-neon-lime');
    
    switch(cmd) {
        case 'help':
            appendConsoleLine('Available Diagnostics Commands:');
            appendConsoleLine('  about   - Diagnostics of the NextEngine Core.');
            appendConsoleLine('  github  - View saias-o/NextEngine on Github.');
            appendConsoleLine('  changelog - View live engine release updates.');
            appendConsoleLine('  llm     - Show dynamic cognitive agent bindings.');
            appendConsoleLine('  sound   - Toggle synthesized terminal sound system.');
            appendConsoleLine('  clear   - Purge screen buffer history.');
            appendConsoleLine('  matrix  - Run retro grid cascade.');
            break;
            
        case 'about':
            appendConsoleLine('NextEngine v0.1.0. Built natively for Vulkan & multi-threading.');
            appendConsoleLine('Equipped with native cognitive layers linking visual geometry databases directly to AI agents (LLMs). Fully customizable via human-tailored C++ APIs.');
            break;
            
        case 'changelog':
            appendConsoleLine('Querying GitHub Releases API...', 'text-cyan');
            setTimeout(() => {
                switchTab('changelog');
            }, 800);
            break;
            
        case 'github':
            appendConsoleLine('Initializing connection to GitHub repositories...');
            appendConsoleLine('Opening https://github.com/saias-o/NextEngine ...', 'text-cyan');
            setTimeout(() => {
                window.open('https://github.com/saias-o/NextEngine', '_blank');
            }, 1000);
            break;
            
        case 'llm':
            appendConsoleLine('Agent binding schemas active.');
            appendConsoleLine('Structure format output below:');
            appendConsoleLine('  - API Endpoint: /api/v1/cognitive/mesh');
            appendConsoleLine('  - Engine Port: localhost:8090 (TCP)');
            appendConsoleLine('  - Actions supported: load_scene, compile_shader, spawn_mesh.');
            break;
            
        case 'sound':
            toggleSoundSystem();
            break;
            
        case 'clear':
            screen.innerHTML = '';
            // Re-append help tip
            appendConsoleLine('&gt; Console buffer cleared.', 'text-cyan');
            break;
            
        case 'matrix':
            runMatrixCascade();
            break;
            
        default:
            appendConsoleLine(`ne_shell: command not found: '${cmd}'. Type 'help' for support.`, 'status-indicator');
    }
    
    // Scroll to bottom
    screen.scrollTop = screen.scrollHeight;
}

function appendConsoleLine(text, className = 'output-line') {
    const screen = document.getElementById('terminal-screen');
    const inputLine = screen.querySelector('.input-line');
    
    const div = document.createElement('div');
    div.className = `term-line ${className}`;
    div.innerHTML = text;
    
    screen.insertBefore(div, inputLine);
}

function runMatrixCascade() {
    let frames = 0;
    const interval = setInterval(() => {
        let line = '';
        for (let i = 0; i < 40; i++) {
            line += Math.random() > 0.5 ? Math.floor(Math.random() * 2) : String.fromCharCode(33 + Math.floor(Math.random() * 93));
        }
        appendConsoleLine(line, 'text-neon-lime');
        document.getElementById('terminal-screen').scrollTop = document.getElementById('terminal-screen').scrollHeight;
        frames++;
        if (frames > 25) clearInterval(interval);
    }, 60);
}


// --- Controls & Toggles (CRT / Sound) ---
function initControls() {
    const crtBtn = document.getElementById('btn-toggle-crt');
    const soundBtn = document.getElementById('btn-toggle-sound');
    
    // CRT toggle
    if (crtBtn) {
        crtBtn.addEventListener('click', () => {
            state.crtEnabled = !state.crtEnabled;
            const indicator = crtBtn.querySelector('.status-indicator');
            if (state.crtEnabled) {
                document.body.classList.add('crt-enabled');
                indicator.innerText = 'ON';
                indicator.classList.add('active');
            } else {
                document.body.classList.remove('crt-enabled');
                indicator.innerText = 'OFF';
                indicator.classList.remove('active');
            }
            playClickSound();
        });
        
        crtBtn.addEventListener('mouseenter', () => playHoverSound());
    }
    
    // Sound toggle
    if (soundBtn) {
        soundBtn.addEventListener('click', () => {
            toggleSoundSystem();
        });
        
        soundBtn.addEventListener('mouseenter', () => playHoverSound());
    }
}

function toggleSoundSystem() {
    state.soundEnabled = !state.soundEnabled;
    const soundBtn = document.getElementById('btn-toggle-sound');
    if (!soundBtn) return;
    
    const indicator = soundBtn.querySelector('.status-indicator');
    
    if (state.soundEnabled) {
        // Unlock browser audio context
        if (!audioCtx) {
            initAudio();
        }
        if (audioCtx && audioCtx.state === 'suspended') {
            audioCtx.resume();
        }
        toggleAmbientPad(true);
        indicator.innerText = 'ON';
        indicator.classList.add('active');
        
        // Play initialization chirp
        setTimeout(() => playClickSound(), 100);
        appendConsoleLine('&gt; synthesized system sound ON', 'text-cyan');
    } else {
        toggleAmbientPad(false);
        indicator.innerText = 'OFF';
        indicator.classList.remove('active');
        appendConsoleLine('&gt; synthesized system sound OFF', 'status-indicator');
    }
}


// --- Direct Sponsor Button setup ---
function initSponsorship() {
    const sponsorBtn = document.getElementById('btn-donate-github');
    if (sponsorBtn) {
        sponsorBtn.addEventListener('mouseenter', () => playHoverSound());
        sponsorBtn.addEventListener('click', () => playClickSound());
    }
}

// --- Documentation Sub-navigation setup ---
function initDocNavigation() {
    const docItems = document.querySelectorAll('.doc-sidebar-item');
    const docSections = document.querySelectorAll('.doc-section');
    
    docItems.forEach(item => {
        item.addEventListener('click', () => {
            playClickSound();
            
            // Deactivate all
            docItems.forEach(i => i.classList.remove('active'));
            docSections.forEach(s => s.classList.remove('active'));
            
            // Activate clicked
            item.classList.add('active');
            const sectionId = item.getAttribute('data-doc');
            const targetSection = document.getElementById(sectionId);
            if (targetSection) {
                targetSection.classList.add('active');
            }
        });
        
        item.addEventListener('mouseenter', () => playHoverSound());
    });
}


// --- Main Entry point ---
window.addEventListener('DOMContentLoaded', () => {
    // Slogan Typewriter effect
    const slogan = document.getElementById('slogan-typing');
    if (slogan) {
        const text = slogan.innerText;
        slogan.innerText = '';
        let index = 0;
        
        function type() {
            if (index < text.length) {
                slogan.innerText += text.charAt(index);
                index++;
                setTimeout(type, 65);
            }
        }
        // Start typewriter
        setTimeout(type, 500);
    }
    
    // Initialize WebGL Graphics
    init3D();
    
    // Initialize Navigation SPA clicks
    initNavigation();
    
    // Initialize Doc Navigation clicks
    initDocNavigation();
    
    // Initialize Terminal Cli event hooks
    initTerminal();
    
    // Initialize CRT & sound toggle button setups
    initControls();
    
    // Initialize direct sponsorship audio triggers
    initSponsorship();
});
