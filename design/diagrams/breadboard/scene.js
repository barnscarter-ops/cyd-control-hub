/* Control Hub - fixed development arrangement (3D). Proposal only. */
(function () {
  const T = THREE, K = Kit, P = Parts;
  const ctx = K.boot({
    components: window.DIAGRAM_COMPONENTS,
    camera: { pos: [40, 150, 200], target: [20, 5, -35] },
    shadowExtent: 300,
    absentNote: 'Not part of the development arrangement. See the proposed assembly view.',
    tiers: {
      'CH-01': [['photo', 'photo-confirmed board'], ['owner', 'owner-reported quantity']],
      'CH-02': [['proposed', 'proposed - inspect supply and lead']],
      'CH-03': [['owner', 'owner-reported kit']],
      'CH-06': [['proposed', 'separate project - LAN API only']],
    },
    nets: [
      ['USB power', 'CH-02 5 V supply → USB-C lead → CH-01 USB-C port', 'v5', 'CH-02'],
      ['Wi-Fi LAN', 'CH-01 ⇄ CH-06 over 2.4 GHz, HTTP/JSON API v1', 'net', 'CH-06'],
      ['Service link (optional)', 'CH-01 I2C connector (GND, SCL IO25, SDA IO32, 3.3 V) → CH-03 22 AWG jumpers → test breadboard', 'sig', 'CH-03'],
    ],
  });
  const scene = ctx.scene;
  const gPow = new T.Group(), gNet = new T.Group(), gSvc = new T.Group();
  scene.add(gPow, gNet, gSvc);
  ctx.addLayer('pow', 'USB power', gPow, true);
  ctx.addLayer('net', 'Wi-Fi link', gNet, true);
  ctx.addLayer('svc', 'Service wiring', gSvc, true);
  ctx.addView("Overview", [40, 150, 200], [20, 5, -35]);
  ctx.addView('Board', [0, 110, 120], [0, 6, 0]);
  ctx.addView('Top', [0, 330, 20], [0, 0, -20]);
  const lbl = (t, x, y, z, s, o) => { const l = K.label(t, Object.assign({ size: s || 5.5 }, o)); l.position.set(x, y, z); ctx.labels.add(l); return l; };

  const bench = new T.Mesh(new T.PlaneGeometry(1400, 1000), K.mat(0x172832, { roughness: 0.9 }));
  bench.rotation.x = -Math.PI / 2; bench.position.y = -0.4; bench.receiveShadow = true; scene.add(bench);
  const grid = new T.GridHelper(1000, 50, 0x2c4553, 0x213641); grid.position.y = -0.3; grid.material.transparent = true; grid.material.opacity = 0.5; scene.add(grid);

  // CH-01 on two foam feet (development arrangement, screen up)
  const cyd = P.cyd(); cyd.position.set(0, 9, 0); scene.add(cyd); ctx.reg('CH-01', cyd);
  [-38, 38].forEach((x) => K.box(scene, [14, 8, 50], [x, 4, 0], 0x2b3438));
  lbl('CH-01  3.5" CYD dashboard', 0, 30, 6, 6);
  lbl('USB-C', -52, 22, 14, 4, { border: '#ff6b9d' });
  lbl('I2C', 22, 20, -31, 4); lbl('SPI', 14, 26, -31, 4); lbl('UART / BAT / SD on bottom edge', 10, 20, 34, 4);

  // CH-02 supply + USB-C lead
  const psu = new T.Group();
  K.box(psu, [42, 30, 28], [0, 15, 0], 0xecebe4); K.box(psu, [4, 0.6, 12], [21.5, 20, -5], 0xb9bfc3, { metalness: 0.8 }); K.box(psu, [4, 0.6, 12], [21.5, 10, -5], 0xb9bfc3, { metalness: 0.8 });
  K.box(psu, [8, 5, 10], [-21.6, 15, 0], 0xc9cdd0, { metalness: 0.8 });
  psu.position.set(-190, 0, 30); gPow.add(psu); ctx.reg('CH-02', psu);
  lbl('CH-02  5 V USB supply', -190, 38, 30, 5.5);
  const lead = K.tube(gPow, [[-169, 15, 30], [-140, 6, 40], [-95, 4, 34], [-62, 10, 18], [-55, 11, 3], [-54, 11, 0]], 0x1b1f22, 2.1, { tension: 0.4 });
  ctx.reg('CH-02', lead); K.box(gPow, [12, 6, 9], [-56, 11, 0], 0x2a2f31);

  // CH-03 optional test link to a small breadboard
  const mb = new T.Group();
  const tex = K.tex(600, 360, (c, w, h) => {
    c.fillStyle = '#ece6d6'; c.fillRect(0, 0, w, h); c.fillStyle = '#cfc7b1'; c.fillRect(0, h / 2 - 12, w, 24);
    for (let i = 0; i < 23; i++) for (let r = 0; r < 10; r++) { const z = r < 5 ? 60 + r * 25 : 190 + (r - 5) * 25 + 20; c.fillStyle = '#0a0b0c'; c.fillRect(30 + i * 24, z, 9, 9); }
  });
  const m = [0, 1, 2, 3, 4, 5].map(() => K.mat(0xdad3bf)); m[2] = new T.MeshStandardMaterial({ map: tex, roughness: 0.75 });
  const mbBox = K.box(mb, [60, 8, 36], [0, 4, 0], m[0]); mbBox.material = m;
  mb.position.set(30, 0, -100); scene.add(mb); ctx.reg('CH-03', mb);
  lbl('optional test breadboard', 30, 20, -100, 4.6);
  const cols = [0xffffff, 0x2b3236, 0x6be07c, 0x58b6ff];
  [0, 1, 2, 3].forEach((i) => {
    ctx.reg('CH-03', K.jumper(gSvc, [22 - 3.81 + i * 2.54, 12, -30], [12 + i * 5 + 8, 8, -88], cols[i], 16 + i * 2, 0.7));
  });
  lbl('CH-03  22 AWG service wires', 12, 24, -60, 4.6);

  // CH-06 separate controller with Wi-Fi arcs
  const sf = new T.Group();
  const tx = K.tex(480, 240, (c, w, h) => { c.fillStyle = '#1f3946'; c.fillRect(0, 0, w, h); c.fillStyle = '#e8f3f5'; c.font = '700 42px system-ui,sans-serif'; c.fillText('Server Fan', 24, 86); c.fillText('Controller', 24, 140); c.fillStyle = '#93aab2'; c.font = '28px system-ui,sans-serif'; c.fillText('separate project', 24, 196); });
  const sm = [0, 1, 2, 3, 4, 5].map(() => K.mat(0x1c2c35)); sm[2] = new T.MeshStandardMaterial({ map: tx, roughness: 0.6 });
  const sb = K.box(sf, [70, 22, 42], [0, 11, 0], sm[0]); sb.material = sm;
  sf.position.set(190, 0, -20); scene.add(sf); ctx.reg('CH-06', sf);
  lbl('CH-06  Server Fan Controller', 190, 30, -20, 5.5);
  for (let k = 0; k < 3; k++) {
    const pts = []; for (let i = 0; i <= 24; i++) { const t = i / 24; pts.push(new T.Vector3(52 + t * 100, 30 + k * 6 + Math.sin(t * Math.PI) * (28 + k * 8), -12 - t * 4)); }
    for (let i = 0; i < 24; i += 2) { const seg = new T.CatmullRomCurve3(pts.slice(i, i + 2)); const mm = new T.Mesh(new T.TubeGeometry(seg, 2, 0.9, 6, false), new T.MeshBasicMaterial({ color: 0x55d8e8 })); mm.userData.pid = 'CH-06'; gNet.add(mm); }
  }
  lbl('Wi-Fi LAN · HTTP/JSON API v1', 100, 66, -14, 5, { border: '#55d8e8' });
  ctx.finish();
  ctx.select('CH-01');
})();
