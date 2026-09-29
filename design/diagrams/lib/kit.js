/* Shared 3D diagram kit. Classic script (works from file://). Requires three.min.js + OrbitControls.js.
   Units are millimetres, Y is up. Diagrams are design proposals, never proof of wiring, pin order, rating or fit. */
(function () {
  const T = THREE;
  const Kit = {};

  Kit.mat = (c, o) => new T.MeshStandardMaterial(Object.assign({ color: c, roughness: 0.55, metalness: 0.05 }, o || {}));
  const asMat = (c, o) => (c && c.isMaterial ? c : Kit.mat(c, o));

  Kit.box = (p, s, pos, c, o) => {
    const m = new T.Mesh(new T.BoxGeometry(s[0], s[1], s[2]), asMat(c, o));
    m.position.set(pos[0], pos[1], pos[2]);
    m.castShadow = m.receiveShadow = true;
    p.add(m);
    return m;
  };
  // axis: 'y' (default), 'x' or 'z' is the cylinder's long axis
  Kit.cyl = (p, r, h, pos, c, o) => {
    o = o || {};
    const g = new T.CylinderGeometry(o.r2 == null ? r : o.r2, r, h, o.seg || 28);
    const m = new T.Mesh(g, asMat(c, o.m));
    if (o.axis === 'x') m.rotation.z = Math.PI / 2;
    if (o.axis === 'z') m.rotation.x = Math.PI / 2;
    m.position.set(pos[0], pos[1], pos[2]);
    m.castShadow = m.receiveShadow = true;
    p.add(m);
    return m;
  };
  Kit.tube = (p, pts, c, r, o) => {
    o = o || {};
    const curve = new T.CatmullRomCurve3(pts.map((v) => new T.Vector3(v[0], v[1], v[2])), false, 'catmullrom', o.tension == null ? 0.15 : o.tension);
    const m = new T.Mesh(new T.TubeGeometry(curve, Math.max(16, pts.length * 14), r || 0.8, 8, false), asMat(c, { roughness: 0.4 }));
    m.castShadow = true;
    p.add(m);
    return m;
  };
  // arched jumper between two points
  Kit.jumper = (p, a, b, c, lift, r) => {
    lift = lift || 8;
    const mid = [(a[0] + b[0]) / 2, Math.max(a[1], b[1]) + lift, (a[2] + b[2]) / 2];
    const up = (v, f) => [v[0], v[1] + lift * f, v[2]];
    return Kit.tube(p, [a, up(a, 0.6), [mid[0] * 0.5 + a[0] * 0.5, mid[1], mid[2] * 0.5 + a[2] * 0.5], mid, [mid[0] * 0.5 + b[0] * 0.5, mid[1], mid[2] * 0.5 + b[2] * 0.5], up(b, 0.6), b], c, r || 0.75);
  };
  Kit.tex = (w, h, draw) => {
    const cv = document.createElement('canvas');
    cv.width = w; cv.height = h;
    draw(cv.getContext('2d'), w, h);
    const t = new T.CanvasTexture(cv);
    t.anisotropy = 8;
    t.encoding = T.sRGBEncoding;
    return t;
  };
  Kit.label = (text, o) => {
    o = o || {};
    const size = o.size || 6;
    const fs = 44;
    const cv = document.createElement('canvas');
    const c = cv.getContext('2d');
    c.font = '600 ' + fs + 'px system-ui,Segoe UI,sans-serif';
    const w = Math.ceil(c.measureText(text).width) + 30;
    cv.width = w; cv.height = fs + 22;
    c.font = '600 ' + fs + 'px system-ui,Segoe UI,sans-serif';
    c.fillStyle = o.bg || 'rgba(8,18,24,.86)';
    c.strokeStyle = o.border || 'rgba(94,231,242,.75)';
    c.lineWidth = 3;
    const r = 14, W = cv.width - 3, H = cv.height - 3;
    c.beginPath(); c.moveTo(3 + r, 3); c.arcTo(W, 3, W, H, r); c.arcTo(W, H, 3, H, r); c.arcTo(3, H, 3, 3, r); c.arcTo(3, 3, W, 3, r); c.closePath();
    c.fill(); c.stroke();
    c.fillStyle = o.color || '#e8f3f5';
    c.textBaseline = 'middle';
    c.fillText(text, 15, cv.height / 2 + 2);
    const t = new T.CanvasTexture(cv);
    t.encoding = T.sRGBEncoding;
    const s = new T.Sprite(new T.SpriteMaterial({ map: t, depthTest: false, transparent: true }));
    s.scale.set((size * cv.width) / cv.height, size, 1);
    s.renderOrder = 20;
    s.center.set(0.5, 0);
    return s;
  };
  Kit.arrowLine = (p, a, b, color) => Kit.tube(p, [a, b], color, 0.5);

  Kit.boot = function (cfg) {
    const stage = document.getElementById('stage');
    const renderer = new T.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
    renderer.setPixelRatio(Math.min(devicePixelRatio || 1, 2));
    renderer.outputEncoding = T.sRGBEncoding;
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = T.PCFSoftShadowMap;
    stage.appendChild(renderer.domElement);
    const scene = new T.Scene();
    scene.background = new T.Color(cfg.background || 0x0d1820);
    scene.fog = new T.Fog(scene.background, 900, 2200);
    const camera = new T.PerspectiveCamera(38, 1, 1, 4000);
    const controls = new T.OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.08;
    controls.maxPolarAngle = Math.PI * 0.495;
    controls.minDistance = 40;
    controls.maxDistance = 1500;
    controls.screenSpacePanning = true;

    scene.add(new T.HemisphereLight(0xdfefff, 0x1b2a33, 0.85));
    const sun = new T.DirectionalLight(0xffffff, 0.95);
    sun.position.set(-160, 320, 220);
    sun.castShadow = true;
    sun.shadow.mapSize.set(2048, 2048);
    const sc = sun.shadow.camera, ex = cfg.shadowExtent || 320;
    sc.left = -ex; sc.right = ex; sc.top = ex; sc.bottom = -ex; sc.near = 50; sc.far = 900;
    sun.shadow.bias = -0.0004;
    scene.add(sun);
    const fill = new T.DirectionalLight(0x9fd7ff, 0.35);
    fill.position.set(220, 120, -200);
    scene.add(fill);

    const ctx = { scene, camera, controls, renderer, registry: {}, layers: {}, selected: null, helpers: [], labels: new T.Group() };
    scene.add(ctx.labels);

    // ---- registry / selection
    ctx.reg = (id, obj) => {
      (ctx.registry[id] = ctx.registry[id] || []).push(obj);
      obj.traverse((o) => { if (o.isMesh && !o.userData.pid) o.userData.pid = id; });
      return obj;
    };
    const byId = new Map(cfg.components.map((c) => [c.id, c]));
    const els = { title: document.getElementById('title'), details: document.getElementById('details'), nets: document.getElementById('nets'), rail: document.getElementById('rail'), hover: document.getElementById('hover') };
    const tierChip = (t) => `<span class="chip ${t[0]}">${t[1]}</span>`;
    ctx.select = (id, fromRail) => {
      const c = byId.get(id) || (cfg.fixtures && cfg.fixtures[id]);
      ctx.helpers.forEach((h) => scene.remove(h));
      ctx.helpers = [];
      ctx.selected = id;
      els.rail.querySelectorAll('button').forEach((b) => b.classList.toggle('on', b.dataset.id === id));
      const objs = ctx.registry[id] || [];
      objs.forEach((o) => {
        const bx = new T.Box3().setFromObject(o);
        if (bx.isEmpty()) return;
        bx.expandByScalar(1.2);
        const h = new T.Box3Helper(bx, 0x5ee7f2);
        h.material.depthTest = false;
        h.renderOrder = 30;
        scene.add(h);
        ctx.helpers.push(h);
      });
      if (!c) { els.title.textContent = id; els.details.innerHTML = ''; return; }
      const tiers = (cfg.tiers && cfg.tiers[id]) || [['proposed', 'proposed']];
      const rows = [['Model / value', c.model_or_value], ['Purpose', c.purpose], ['Connections', c.connections], ['Inventory status', c.inventory_status], ['Evidence', c.evidence]];
      const drawn = objs.length ? '' : `<dt>In this view</dt><dd><span class="chip off">not drawn here</span> ${cfg.absentNote || ''}</dd>`;
      const note = cfg.notes && cfg.notes[id] ? `<dt>Shown as</dt><dd>${cfg.notes[id]}</dd>` : '';
      els.title.textContent = `${c.id} — ${c.name}`;
      els.details.innerHTML = `<div class="chips">${tiers.map(tierChip).join('')}<span class="chip off">not bench-verified</span></div>` +
        rows.map((r) => `<dt>${r[0]}</dt><dd>${r[1]}</dd>`).join('') + note + drawn;
      if (fromRail && objs.length) {
        const bx = new T.Box3(); objs.forEach((o) => bx.expandByObject(o));
        const ctr = bx.getCenter(new T.Vector3());
        controls.target.lerp(ctr, 0.6);
      }
    };
    ctx.finish = () => {
      // parts rail
      cfg.components.forEach((c) => {
        const b = document.createElement('button');
        b.textContent = c.id; b.dataset.id = c.id; b.title = c.name;
        if (!ctx.registry[c.id]) b.classList.add('absent');
        b.addEventListener('click', () => ctx.select(c.id, true));
        els.rail.appendChild(b);
      });
      // nets table
      if (cfg.nets) {
        els.nets.innerHTML = '<tr><th>Net</th><th>From → to</th><th>Class</th></tr>' + cfg.nets.map((n) =>
          `<tr ${n[3] ? `data-id="${n[3]}"` : ''}><td class="k k-${n[2]}">${n[0]}</td><td>${n[1]}</td><td class="k-${n[2]}">${{ sig: 'signal', pwr: '12 V load', gnd: 'ground', '3v3': '3.3 V', v5: '5 V', net: 'network' }[n[2]] || n[2]}</td></tr>`).join('');
        els.nets.querySelectorAll('tr[data-id]').forEach((r) => r.addEventListener('click', () => ctx.select(r.dataset.id, true)));
      }
      // picking meshes
      const pick = [];
      Object.keys(ctx.registry).forEach((k) => ctx.registry[k].forEach((o) => o.traverse((m) => { if (m.isMesh) pick.push(m); })));
      ctx.pick = pick;
    };

    // ---- layers / toolbar
    const tb = document.getElementById('toolbar');
    ctx.addView = (name, pos, target) => {
      const b = document.createElement('button');
      b.textContent = name;
      b.addEventListener('click', () => { camera.position.set(pos[0], pos[1], pos[2]); controls.target.set(target[0], target[1], target[2]); controls.update(); });
      tb.appendChild(b);
    };
    ctx.addLayer = (id, label, obj, on) => {
      const l = document.createElement('label');
      const i = document.createElement('input');
      i.type = 'checkbox'; i.checked = on !== false;
      obj.visible = i.checked;
      i.addEventListener('change', () => { obj.visible = i.checked; });
      l.appendChild(i); l.appendChild(document.createTextNode(label));
      tb.appendChild(l);
      ctx.layers[id] = obj;
    };
    ctx.addLayer('labels', 'Labels', ctx.labels, true);

    // ---- pointer picking (ignore drags)
    const ray = new T.Raycaster(), mv = new T.Vector2();
    const hit = (ev) => {
      const r = renderer.domElement.getBoundingClientRect();
      mv.set(((ev.clientX - r.left) / r.width) * 2 - 1, -((ev.clientY - r.top) / r.height) * 2 + 1);
      ray.setFromCamera(mv, camera);
      const list = (ctx.pick || []).filter((m) => { let o = m; while (o) { if (o.visible === false) return false; o = o.parent; } return true; });
      const hs = ray.intersectObjects(list, false);
      const solid = hs.find((h) => !h.object.material.userData.ghost);
      return (solid || hs[0] || {}).object;
    };
    let down = null;
    const cv = renderer.domElement;
    cv.addEventListener('pointerdown', (e) => { down = [e.clientX, e.clientY]; });
    cv.addEventListener('pointerup', (e) => {
      if (!down || Math.hypot(e.clientX - down[0], e.clientY - down[1]) > 5) return;
      const o = hit(e);
      if (o) ctx.select(o.userData.pid);
    });
    let last = 0;
    cv.addEventListener('pointermove', (e) => {
      const n = performance.now(); if (n - last < 60 || e.buttons) return; last = n;
      const o = hit(e);
      cv.style.cursor = o ? 'pointer' : 'grab';
      if (o) { const c = byId.get(o.userData.pid) || (cfg.fixtures && cfg.fixtures[o.userData.pid]); els.hover.textContent = o.userData.pid + (c ? ' · ' + c.name : ''); els.hover.style.opacity = 1; } else els.hover.style.opacity = 0;
    });
    cv.addEventListener('pointerleave', () => { els.hover.style.opacity = 0; });

    // ---- sizing + loop
    const resize = () => {
      const w = stage.clientWidth, h = stage.clientHeight;
      renderer.setSize(w, h, false);
      camera.aspect = w / h; camera.updateProjectionMatrix();
    };
    new ResizeObserver(resize).observe(stage);
    resize();
    camera.position.set(...cfg.camera.pos);
    controls.target.set(...cfg.camera.target);
    controls.update();
    (function loop() { controls.update(); renderer.render(scene, camera); requestAnimationFrame(loop); })();
    window.__ctx = ctx;
    return ctx;
  };

  window.Kit = Kit;
})();
